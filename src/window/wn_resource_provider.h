#ifndef SC3_WINDOW_WN_RESOURCE_PROVIDER_H
#define SC3_WINDOW_WN_RESOURCE_PROVIDER_H

#include "audio/api.h"
#include "fonts/sc3_font_manager.h"
#include "graphics/image.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Recovered cIGZCtrlCreator prefix: Unknown0..8, CLSID12 and callbacks16/20/24.
// The original interface also has EnumCtrls and two debug getters at28..36; their declarations remain unresolved.
// The concrete primary deleting destructor is separate from this creator view.
class WnResourceClient : public ScUnknown {
public:
  virtual u32 GetCLSID() = 0;
  virtual u8 OnRegistered() = 0;
  virtual u8 OnRemoved() = 0;
  virtual u8 CreateAndSetup(u32 kind, u32 iid, void **out) = 0;
};

// Actual resource-provider fields replace six separately configured map adapters and synthetic owner-lifetime profiles.
class WnResourceProvider : public WnSystemService, public WnResources {
public:
  WnResourceProvider();
  ~WnResourceProvider() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  u32 GetColor(i32 identifier) override;
  void GetColor(i32 identifier, u8 *red, u8 *green, u8 *blue) override;
  void SetColor(i32 identifier, u8 red, u8 green, u8 blue) override;
  void SetColor(i32 identifier, u32 color) override;
  FtFont *GetFont(i32 identifier) override;
  void SetFont(i32 identifier, FtFont *font) override;
  GxPaletteView *SystemPalette(i32 identifier) override;
  void SetSystemPalette(i32 identifier, GxPaletteView *palette) override;
  GxBuffer *GetImage(i32 identifier) override;
  void SetImage(i32 identifier, GxBuffer *image) override;
  u32 GetSoundId(i32 identifier) override;
  void SetSoundId(i32 identifier, u32 sound) override;
  i32 GetVolume() override;
  void SetVolume(i32 volume) override;
  const ScStringView *GetText(i32 identifier) override;
  void SetText(i32 identifier, const ScStringView *text) override;
  bool RegisterObject(WnResourceClient *client) override;
  bool RemoveObject(WnResourceClient *client) override;
  WnButton *CreateButton(u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle,
      u32 key_equivalent) override;
  WnButton *CreateFixedButton(u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle,
      u32 key_equivalent) override;
  WnButton *CreateToggleButton(u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle,
      u32 key_equivalent) override;
  WnLabelControl *CreateLabel(u32 identifier, const ScStringView &caption) override;
  WnOptionControl *CreateOptGrp(u32 identifier, const ScStringView &caption, const ScRect *rectangle) override;
  WnSliderControl *CreateSlider(u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) override;
  WnScrollbarControl *CreateScrollbar(u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) override;
  WnSpinnerControl *CreateSpinner(u32 identifier, i32 type, i32 minimum, i32 maximum, i32 step, i32 value) override;
  WnTextControl *CreateTextEdit(u32 identifier, u8 editable) override;
  WnButton *CreateFixedToggleButton(u32 identifier, const ScStringView &caption, GxBuffer *image,
      const ScRect *rectangle, u32 key_equivalent) override;
  WnButton *CreateCheckBox(u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle,
      u32 key_equivalent) override;
  WnButton *CreateRadioButton(u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle,
      u32 key_equivalent) override;
  WnBitmapControl *CreateWinBMP(u32 identifier, GxBuffer *image, bool fit) override;
  bool CreateObject(u32 kind, u32 iid, void **out) override;

protected:
  struct WnResourceNode {
    WnResourceNode *next, *previous;
    WnResourceClient *client;
  };

  struct WnResourceList {
    WnResourceList();
    ~WnResourceList();
    WnResourceNode *head;
  };

  bool initialized;
  i32 volume;
  FtFontSystem *font_manager;
  AuSoundDevice *sound_device;
  WnResourceMap<u32> colors;
  WnResourceMap<FtFont *> fonts;
  WnResourceMap<GxPaletteView *> palettes;
  WnResourceMap<GxBuffer *> images;
  WnResourceMap<u32> sound_ids;
  WnResourceMap<const ScStringView *> texts;
  WnResourceList objects;
};

#endif
