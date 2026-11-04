#ifndef SC3_UI_QUERY_DIALOGS_H
#define SC3_UI_QUERY_DIALOGS_H

#include "city/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/text_controls.h"

#ifdef GetObject
#undef GetObject
#endif
#ifdef CreateDialog
#undef CreateDialog
#endif
class WnOptionControl;

// Layer-dialog controls identify the installed captions and the original view-selection groups.
enum UiLayerControlId : u32 {
  kUiLayerTransportCheckId = 0x3e8,
  kUiLayerPowerLinesCheckId = 0x3e9,
  kUiLayerFloraCheckId = 0x3ea,
  kUiLayerZonedBuildingsCheckId = 0x3eb,
  kUiLayerOtherBuildingsCheckId = 0x3ec,
  kUiLayerZonesCheckId = 0x3ed,
  kUiUndergroundLayerOptionGroupId = 0x2711,
  kUiDataLayerOptionGroupId = 0x2712,
  kUiAboveGroundLayerSectionId = 0x2904,
  kUiUndergroundLayerSectionId = 0x2905,
  kUiDataLayerSectionId = 0x2906,
  kUiLayerDefaultButtonId = 0x2af8,
  kUiLayerApplyButtonId = 0x2af9,
  kUiLayerAcceptButtonId = 0x2afa,
  kUiLayerTerrainGridButtonId = 0x2afb,
  kUiLayerSubwayOptionId = 0x7d0,
  kUiLayerWaterPipesOptionId = 0x7d1,
  kUiLayerAuraOptionId = 0xbb8,
  kUiLayerCrimeOptionId = 0xbb9,
  kUiLayerDensityOptionId = 0xbba,
  kUiLayerPowerOptionId = 0xbbb,
  kUiLayerFlammabilityOptionId = 0xbbc,
  kUiLayerLandValueOptionId = 0xbbd,
  kUiLayerPollutionOptionId = 0xbbe,
  kUiLayerTrafficOptionId = 0xbbf,
  kUiAboveGroundLayerOutlineId = 0x75ccd30c,
  kUiUndergroundLayerOutlineId = 0x75ccd30d,
  kUiDataLayerOutlineId = 0x75ccd30e,
  kUiLayerFooterOutlineId = 0x75ccd30f,
};

// Preview selection is a command payload kind; the outer window message remains WnMsg_Command.
enum UiPreviewCommand : u32 {
  kUiPreviewOriginalSelected = 0x4588a55b,
  kUiPreviewReplacementSelected = 0x4588a55a,
};

// Installed cISC3WinLayers primary interface. Byte-valued results retain their observed PC width.
class UiLayerDialogInterface : public ScUnknown {
public:
  virtual u8 CreateDialog() = 0;
  virtual WnWindow *GetAssociatedIGZWin() const = 0;
  virtual u8 GetViewItem(u32 identifier) = 0;
  virtual u8 SetViewItem(u32 identifier, bool enabled) = 0;
  virtual u32 GetView() = 0;
  virtual u8 SetView(u32 identifier) = 0;
  virtual u32 GetViewOption(u32 identifier) = 0;
  virtual u8 SetViewOption(u32 identifier, u32 value) = 0;
  virtual ~UiLayerDialogInterface() = default;
};

// The 24-byte layer dialog owns a generic window and a separate retained control.
// The window and callback are retained by the real generic-window interface after dialog creation.
class UiLayerDialog : public UiLayerDialogInterface, public UiWindowCallback {
public:
  UiLayerDialog();
  ~UiLayerDialog() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateDialog() override;
  WnWindow *GetAssociatedIGZWin() const override;
  u8 GetViewItem(u32 identifier) override;
  u8 SetViewItem(u32 identifier, bool enabled) override;
  u32 GetView() override;
  u8 SetView(u32 identifier) override;
  u32 GetViewOption(u32 identifier) override;
  u8 SetViewOption(u32 identifier, u32 value) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;

protected:
  u32 references;
  UiGenericInterface *window;
  u8 changed;
  WnOptionControl *control;
  u8 OnShutdown(WnWindow *recipient);
  u8 ItemClick(u32 identifier);
  u8 EndDialog(WnWindow *recipient, u32 result);
  u8 Apply();
  u8 ResetToDefault();
  u8 GroupSwitch(u32 identifier);
  u8 SetCheck(u32 identifier, bool enabled);
  u8 SetPress(u32 identifier, bool enabled);
  u8 GetCheck(u32 identifier);
  u8 Enable(u32 identifier, bool enabled);
  u8 SetValue(u32 identifier, u32 value);
  u32 GetValue(u32 identifier);
  u8 AddCheck(u32 identifier, u32 x, u32 y, u32 text_instance);
  u8 BeginOptionGroup(u32 identifier);
  u8 AddOption(u32 identifier, u32 x, u32 y, u32 text_instance);
  u8 EndOptionGroup();
  u8 AddRect(WnWindow *parent, u32 identifier);
  u8 AddButton(WnWindow *parent, u32 identifier, u32 group, u32 instance, bool selected);
  u8 OnInit(WnWindow *recipient);
  u8 OnGetMinMaxInfo(WnWindow *recipient, ScRect *limits) const;
  u8 OnAreaChanged(WnWindow *recipient, const ScRect &area);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiLayerDialog) == 24);

class WnWindow;
struct ScRect;

// Original layer-dialog section holds one title, its rectangle, an optional group and its item IDs.
struct UiLayerSection {
  u32 title, outline, option;
  const u32 *items;
  u32 item_count;
};

bool UiLayerFitButton(WnWindow *parent, u32 identifier);
bool UiLayerSectionMeasure(WnWindow *parent, const UiLayerSection &section, ScRect *out);
u8 UiLayerSectionPlace(WnWindow *parent, const UiLayerSection &section, const ScRect &area);

// Reached operations of the original7587239D preview view, represented by actual native multiple inheritance.
class UiPreviewControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual u8 SetBackgroundColor(u32 pixel) = 0;
  virtual u8 SetOriginalColor(u32 pixel) = 0;
  virtual u8 SetReplacementColor(u32 pixel) = 0;
  virtual u8 SetAttributes(GeAttrib *value) = 0;
  virtual u8 SetOverride(OcOverride *value) = 0;
  // Native used-effects view of original slot36. Its private nominal return remains unresolved and is discarded here.
  virtual void SetEnabledEffects(u8 value) = 0;
};

// The private preview owns its badge, two prepared buffers and retained original/replacement attribute views.
class UiPreview : public UiBaseWindow, public UiPreviewControl {
public:
  UiPreview();
  ~UiPreview() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Init() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  u8 SetBackgroundColor(u32 pixel) override;
  u8 SetOriginalColor(u32 pixel) override;
  u8 SetReplacementColor(u32 pixel) override;
  u8 SetAttributes(GeAttrib *value) override;
  u8 SetOverride(OcOverride *value) override;
  void SetEnabledEffects(u8 value) override;

protected:
  u8 enabled;
  u32 key;
  GxBuffer *badge, *original;
  u32 original_color;
  GxBuffer *replacement;
  u32 replacement_color, selected, available;
  GeAttrib *attributes;
  OcOverride *override_attributes;
  bool original_refreshed, replacement_refreshed;
  u8 Select(u32 value);
  u8 SelectAvailable();
  u8 RefreshOriginal();
  u8 RefreshReplacement();
  u8 RenderPreview(GxBuffer *buffer, SpDefinition *definition);
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
};

class WnComboControl;
class WnButton;

// Class57A owns its query interface and secondary callback, rather than inheriting a generic window.
// The real factory is connected; initialization retains its original separate preview helper.
// Its class580 provider branch uses the separate city-sprite preview interface.
class UiQueryDialog : public UiQueryWindow, public UiWindowCallback {
public:
  UiQueryDialog();
  ~UiQueryDialog() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  u8 InitAtLocation(const CtCoord *location, u32 flags) override;
  u8 InitForOccupant(ScOccupant *target, u32 flags) override;
  u8 SetCaption(const ScStringView &text) override;
  WnWindow *GetWindow() const override;
  u8 SetStatusText(const ScStringView &text) override;
  u8 AddOption(const ScStringView &caption, FtFont *font, bool checked) override;
  u8 IsOptionChecked() override;
  void SetLabelFlags(u32 flags) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 SetExtent(i32 value) override;
  u8 SetHeading(ScStringView *text) override;
  u8 SetCaptionEnabled(bool value) override;
  u8 SetBackgroundColor(u32 pixel) override;
  u8 GetLayout(u32 *out) const override;
  u32 GetExtent() const override;
  ScStringView *GetHeading() override;
  ScStringView *GetCaption() override;
  ScStringView *GetAlternateStatusText() override;
  u8 GetCaptionEnabled() override;
  u8 GetBackgroundColor(u32 *out) override;
  u8 QueryPreviewInterface(u32 iid, void **out) const override;
  u8 GetPictureArea(ScRect *out) const override;
  u8 QuerySectionInterface(u32 iid, void **out) const override;
  u8 GetContentArea(ScRect *out) const override;
  u8 SetSection(UiQuery *value) override;
  u8 AddSection(UiQuery *value) override;
  u8 FindSectionInterface(u32 *tag, u32 iid, void **out) override;
  ScOccupant *GetTarget() const override;
  u8 SetStatusPair(ScStringView *first, ScStringView *second) override;
  u8 GetArea(ScRect *out) const override;
  u8 SetArea(ScRect *area) override;
  u8 GetLocation(CtCoord *out) const override;
  u8 SetLayout(u32 mode) override;
  u8 NotifyTarget(CtCity *city, u8 opening) override;
  u8 SetPreviewWindow(WnWindow *preview) override;
  u32 GetSectionCount() override;
  u8 LayoutOption() override;
  u8 Attach(WnWindow *recipient);
  u8 Detach(WnWindow *recipient);
  u8 SelectSection();
  void EndModalEffects(WnWindow *recipient, u32 result);
  u8 Initialize();
  u8 EnsureSection();
  // Original63059, primary100: constructs the class580/IID57F preview for eligible occupants.
  u8 BuildPreview() override;
  u8 UpdateQueryFormat();
  void UpdateLayoutEffects();
  void LayoutPictureEffects(i32 mode);
  void LayoutCaptionEffects(i32 mode);
  void LayoutStatusEffects(i32 mode);
  void LayoutContentEffects(i32 mode);
  void LayoutButtonsEffects(i32 mode);

  u32 references;
  u8 active;
  u32 background, layout, extent;
  ScRect area, picture_area, content_area, caption_area, status_area, button_area;
  u32 attributes;
  ScString heading, caption;
  u8 caption_enabled;
  ScString status, alternate_status;
  u8 status_pair, status_enabled, preview_enabled;
  ScOccupant *target;
  CtCoord location;
  UiGenericInterface *window;
  WnWindow *caption_control, *status_control, *alternate_control;
  WnComboControl *selector;
  WnWindow *preview_window;
  UiQuery *section, *active_section;
  WnButton *action_button;
  ScArray<UiQuery *> sections;
};

bool UiIsReplaceableOccupant(ScOccupant *occupant);

// Real module-owned query collection keeps retained rows and the optional property used for sorted insertion.
class UiDataRows : public UiQueryRows {
public:
  UiDataRows();
  virtual ~UiDataRows();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateIterator(UiQueryIterator **out) override;
  u32 GetCount() const override;
  void SetSortPropertyEffects(u32 key) override;
  u8 AppendRow(UiQueryRow *row) override;
  UiQueryRow *CreateRow() const override;
  u8 Clear() override;

protected:
  u32 references;
  ScArray<UiQueryRow *> rows;
  u32 sort_property;
  bool IsBefore(UiQueryRow *first, UiQueryRow *second);
  friend class UiDataIterator;
};

// The row owns its inserted property holders in unsigned key order; duplicate insertion leaves the old holder in place.
class UiDataRow : public UiQueryRow {
public:
  UiDataRow();
  virtual ~UiDataRow();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetProperty(u32 key, UiQueryProperty **out) override;
  u8 SetProperty(u32 key, UiQueryProperty *value) override;
  UiQueryProperty *CreateProperty() const override;
  u8 SetText(u32 key, ScStringView *value) override;
  u8 SetOptionalText(u32 key, ScStringView *value) override;
  u8 SetObject(u32 key, ScUnknown *value) override;
  u8 SetU32(u32 key, u32 value) override;
  ScStringView *GetText(u32 key) override;
  ScUnknown *GetObject(u32 key) override;
  u32 GetU32(u32 key) override;
  u32 GetType(u32 key) override;

protected:
  struct UiDataEntry {
    u32 key;
    UiQueryProperty *value;
  };

  u32 references;
  ScArray<UiDataEntry> properties;
};

// The iterator retains the concrete collection and reloads its row storage after each virtual count query.
class UiDataIterator : public UiQueryIterator {
public:
  explicit UiDataIterator(UiDataRows *owner);
  virtual ~UiDataIterator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  UiQueryRow *GetFirst() override;
  UiQueryRow *GetCurrent() const override;
  UiQueryRow *GetNext() override;
  UiQueryRow *GetAt(u32 index) override;

protected:
  u32 references;
  UiDataRows *owner;
  u32 index;
};

// The original twelve-byte property holder releases its previous variant before retaining a replacement.
class UiDataProperty : public UiQueryProperty {
public:
  UiDataProperty();
  virtual ~UiDataProperty();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 SetValue(ScVariant *value) override;
  u32 GetValue(ScVariant **out) override;

protected:
  u32 references;
  ScVariant *value;
};

// Real312-byte class584 extends the wrapping list with secondary query264, retained owner and a named category view.
// Its cached fonts are borrowed separately from the two retained fonts in the base list.
class UiQuerySection : public UiTextList, public UiQuery {
public:
  UiQuerySection();
  ~UiQuerySection() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool Init() override;
  bool Shutdown() override;
  u32 *GetTag() override;
  ScStringView *GetTitle() override;
  using WnRootWidget::SetArea;
  void SetArea(const ScRect *area) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::GetArea;
  void GetArea(ScRect *out) override;
  WnWindow *GetWindow() override;
  u8 GetLocation(CtCoord *out) override;
  u8 AddText(const ScStringView *label, const ScStringView *value, u32 categories, u8 warning) override;
  u8 BindTo(UiQueryWindow *owner) override;
  u8 NotifyTarget(CtCity *city, u8 opening) override;
  u8 SetTag(const u32 *tag) override;
  u8 SetTitle(const ScStringView &title) override;
  UiQueryWindow *GetOwner() override;
  ScOccupant *GetTarget() override;
  u8 SetQueryFonts(FtFont *first, FtFont *second) override;
  // Native effects projection of original64695; its nominal return remains unresolved.
  void SetCategoriesEffects(u32 categories) override;

protected:
  UiQueryWindow *query_owner;
  FtFont *cached_fonts[2];
  ScString title;
  u32 tag, categories;
  u8 state308;
  u8 InsertSortedRow(
      const ScStringView *first, const ScStringView *second, u32 first_color, u32 second_color, u32 categories);
};

// Concrete class411164F1 consumes an operation and visibility flag to show or hide the raster-query window.
class UiRasterVisibilityCommand : public UiCommand {
public:
  UiRasterVisibilityCommand();
  bool Execute() override;
  bool SetVisible(bool visible);
};

#endif
