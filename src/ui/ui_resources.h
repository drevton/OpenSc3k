#ifndef SC3_UI_UI_RESOURCES_H
#define SC3_UI_UI_RESOURCES_H

#include "common/sc3_string.h"
#include "graphics/image_codecs.h"
#include "resources/api.h"
#include "ui/controls.h"
#include "window/window.h"
#include "window/wn_resource_provider.h"

// Resource-provider selectors and the actual controls constructed by their registered handlers.
enum UiResourceKind : u32 {
  kUiResourceList = 0x75807646,
  kUiResourceListAlternate = 0x75807650,
  kUiResourceStandardButton = 0x7580764b,
  kUiResourceFlatPanel = 0x75807647,
  kUiResourceCaptionButton = 0x75807649,
  kUiResourceColumns = 0x75807651,
  kUiResourceBuildingPreview = 0x75807652,
  kUiResourceCombo = 0x7580764e,
  kUiResourceReadOnlyCombo = 0x7580764f,
  kUiResourceLabel = 0x7580764d,
  kUiResourceEightStateButton = 0x75807648,
  kUiResourceToggleButton = 0x7580764a,
  kUiResourceOptionGroup = 0x7580764c,
};

// SIMUI's own cGZWinBMP copy uses its module-bound window base and the genuine shared bitmap-control interface.
class UiBitmap : public UiBaseWindow, public WnBitmapControl {
public:
  UiBitmap();
  ~UiBitmap() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  void SetImage(GxBuffer *image) override;
  void SetAlpha(bool enabled, f32 opacity) override;
  bool GetBitmapFlag(i32 mask) const override;
  void SetBitmapFlag(i32 mask, bool enabled) override;
  bool Init() override;
  bool Shutdown() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;

protected:
  GxBuffer *image;
  u8 alpha_enabled;
  f32 opacity;
  ScRect source_area;
  u32 bitmap_flags;
  void SetSourceBufferAreaFromWindowArea(const ScRect &rectangle);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiBitmap) == 200);

class RsStream;

// Original cSC3StringTokenData carries a token name, replacement kind, resource identity and signed numeric value.
class UiTokenData {
public:
  UiTokenData();
  UiTokenData(u32 kind, const ScStringView &name, i32 number);
  UiTokenData(u32 kind, const ScStringView &name, u32 instance);
  UiTokenData(u32 kind, const ScStringView &name, u32 instance, u32 group);
  virtual ~UiTokenData();
  UiTokenData &operator=(const UiTokenData &source);
  bool SerialRead(u32 version, RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 kind;
  ScString name;
  u32 instance, group;
  i32 number;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiTokenData) == 40);

// The primary unknown view remains distinct from the client's inherited unknown identity.
class UiResourceIdentity : public ScUnknown {};

// Actual cSC3CtrlCreator owns its kind dispatch and one provider acquisition; both views share its count.
class UiResourceClient : public UiResourceIdentity, public WnResourceClient {
public:
  UiResourceClient();
  virtual ~UiResourceClient();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetCLSID() override;
  u8 OnRegistered() override;
  u8 OnRemoved() override;
  u8 CreateAndSetup(u32 kind, u32 iid, void **out) override;

protected:
  struct UiResourceKind {
    UiResourceKind *next;
    u32 kind;
    u8 (*create)(WnResources *provider, u32 iid, void **out);
  };

  UiResourceKind *kinds;
  u32 references;
  WnResources *provider;
  void AddKindEffects(u32 kind, u8 (*create)(WnResources *, u32, void **));
};

bool UiQueryResourceClass(u32 clsid, u32 iid, void **out);
struct RsKey;
class GxBuffer;
u8 UiLoadResourceBuffer(const RsKey &key, GxBuffer **out, u8 keyed);
u32 UiMakeResourceColor(ScUnknown *control, u32 red, u32 green, u32 blue);
u8 UiGetResourceOutline(WnResources *provider, u32 *pixels);

// Original scoped resource-manager acquisition also spans the replacement dialog's resource-backed formatting.
class UiResourceManagerRef {
public:
  UiResourceManagerRef();
  ~UiResourceManagerRef();
  RsManager *manager;
};

// Original scoped provider acquisition is shared by image lookup and the two-column window's ellipsis initialization.
class UiWindowResourcesRef {
public:
  UiWindowResourcesRef();
  ~UiWindowResourcesRef();
  WnResources *provider;
};

// Source kind creators retain their separate provider, IID and genuine output arguments.
u8 UiCreateResource75807646(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807650(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764B(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807647(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807649(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807651(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807652(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764E(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764F(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764D(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource75807648(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764A(WnResources *provider, u32 iid, void **out);
u8 UiCreateResource7580764C(WnResources *provider, u32 iid, void **out);

// SIMUI's module-local scoped image reference uses its own resource-manager cache.
class UiImageRef {
public:
  UiImageRef(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~UiImageRef();
  GxBuffer *GetBuffer();
  bool Load(RsManager *manager, u32 group, u32 instance);

protected:
  GxBuffer *buffer;
};

class FtFont;

// The original text reference retains the complete resource key and owns its resolved String interface.
class UiTextRef {
public:
  UiTextRef(u32 instance, u32 group);
  virtual ~UiTextRef();
  bool IsResolved();
  const ScStringView *GetResource() const;
  bool CopyTo(ScStringView *sink);
  ScString GetText();
  ScString GetExpandedText();
  template <class Context>
  bool CopyWithTokens(
      ScStringView *sink, bool(SC3_CDECL *callback)(const ScString &, i32, UiTokenData *, Context *), Context *context);
  static void FormatForEllipsis(ScString *text, i32 width, FtFont *font);

protected:
  ScStringView *text;
  RsKey key;
  bool SubstituteBuiltin(ScString *value, i32 *position);
  bool SubstituteCustom(ScString *value, UiTokenData *data, i32 *position);
  void LoadResource();
  template <class Context>
  void ReplaceParameters(
      ScString *value, bool(SC3_CDECL *callback)(const ScString &, i32, UiTokenData *, Context *), Context *context);
};

// Copy resolved text and interleave built-in expansion with the typed callback at each percent character.
// One token-data object survives the scan; inserted bytes are skipped and the sink is written only after completion.
template <class Context>
bool UiTextRef::CopyWithTokens(
    ScStringView *sink, bool(SC3_CDECL *callback)(const ScString &, i32, UiTokenData *, Context *), Context *context) {
  if (!text)
    return false;
  ScString copy(*text);
  ReplaceParameters(&copy, callback, context);
  sink->FromChar(copy.Data(), copy.Strlen());
  return true;
}

// Expand built-in tokens before consulting the callback, retaining one token-data object for the complete scan.
// Resume after each replacement so percent characters inserted by either replacement are not scanned again.
template <class Context>
void UiTextRef::ReplaceParameters(
    ScString *value, bool(SC3_CDECL *callback)(const ScString &, i32, UiTokenData *, Context *), Context *context) {
  UiTokenData data;
  for (i32 position = 0; (u32)position < value->Strlen(); position = (i32)((u32)position + 1))
    if (value->Data()[(u32)position] == '%' && !SubstituteBuiltin(value, &position) &&
        callback(*value, position, &data, context))
      SubstituteCustom(value, &data, &position);
}

#endif
