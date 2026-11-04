#ifndef SC3_UI_API_H
#define SC3_UI_API_H

#include "city/city.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "resources/resource.h"
#include "ui/graph_window.h"

class AdAdvisor;
class UiAdvisorButtonInterface;

u8 UiFindAdvisor(u32 identifier, AdAdvisor **out);
u8 UiFindAdvisorPortrait(u32 advisor, u32 mood, bool small_portrait, RsKey *out, i32 *frames);
u8 UiCreateAdvisorPicture(u32 identifier, i32 type, i32 frames, const RsKey &key, UiAdvisorButtonInterface **out);
u32 UiAdvisorPictureFrames(u32 instance);

class WnWindow;

// Reached cISC3WinBudgetSlider IID730391B9, created by class230391B9; distinct from WnSliderControl.
// Original slot12 exposes a borrowed window; value and marker accessors retain their separate signed outputs.
// Nominal bool/BYTE return spelling for the private setters and output getters remains unresolved.
class UiBudgetSlider : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 SetValue(i32 value) = 0;                         // Original16.
  virtual i32 GetValue() const = 0;                           // Original20.
  virtual u8 SetMarkerValue(i32 value) = 0;                   // Original24.
  virtual u8 GetMarkerValue(i32 *value) const = 0;            // Original28.
  virtual u8 SetMinMax(i32 minimum, i32 maximum) = 0;         // Original32.
  virtual u8 GetMinMax(i32 *minimum, i32 *maximum) const = 0; // Original36.
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3WinCityViewCellCursor.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class UiCityCursor : public ScMessageTarget {

public:
  virtual bool Init(CtCity *) = 0;
  virtual bool Shutdown() = 0;

  virtual u32 CellCountX() = 0;
  virtual u32 CellCountZ() = 0;
  virtual bool InBounds(CtCoord const &) const = 0;
  virtual bool InBounds(GeBounds const &) const = 0;
  virtual void SetColor(CtCoord const &, u32) = 0;
  virtual void SetColor(GeBounds const &, u32) = 0;
  virtual void GetColor(CtCoord const &, u32 *) = 0;
  virtual void SetPalette(u32) = 0;
  virtual void SetDefaultColor(u32 palette, u32 color) = 0;
  virtual void GetDefaultColor(u32 *palette, u32 *color) = 0;

  virtual bool Clear(bool) = 0;
  virtual void SetCell(CtCoord const &, u32) = 0;
  virtual void SetLine(CtCoord const &, CtCoord const &, u32) = 0;
  virtual void SetRect(GeBounds const &, u32) = 0;
  virtual bool SetEllipse(CtCoord const &, CtCoord const &, u32) = 0;
  virtual void SetCellList(SpSelection *, u32, SpSelection *, u32) = 0;
  virtual bool UpdateRegion(GeBounds const &) = 0;
  virtual bool GetEnumerator(SpSelection **) = 0;
};

// Declared source factories remain explicit dependencies until their actual classes are reconstructed.
ScUnknown *UiCreateClass230391B9();
ScUnknown *UiCreateClass000005A6();
ScUnknown *UiCreateClass3370D1A5();
ScUnknown *UiCreateClass0000057A();
ScUnknown *UiCreateClass00000584();
ScUnknown *UiCreateClass000005A3();
ScUnknown *UiCreateClass000005A9();
ScUnknown *UiCreateClassE3091435();
ScUnknown *UiCreateClass62FA5742();
ScUnknown *UiCreateClass62BE6EE2();
ScUnknown *UiCreateClass72E85997();
ScUnknown *UiCreateClass82E968EE();
ScUnknown *UiCreateClassC2F14704();
ScUnknown *UiCreateClass82F622C1();
ScUnknown *UiCreateClassC2FA5860();
ScUnknown *UiCreateClass030477A4();
ScUnknown *UiCreateClass22BE563E();
ScUnknown *UiCreateClass4302B412();
ScUnknown *UiCreateClass454423FF();
ScUnknown *UiCreateClass43164FB1();
ScUnknown *UiCreateClass02F61B00();
ScUnknown *UiCreateClassE2F95AF5();
ScUnknown *UiCreateClass02F95D57();
ScUnknown *UiCreateClass12FAF305();
ScUnknown *UiCreateClass756B5B54();

class FtFont;

// Original graph font acquisition through the font and language services.
FtFont *UiGraphLoadFont(i32 type, u32 size, u32 flags, u32 color);

struct UiLineGraphSeriesInfo;
// The original argument is a mutable vector<float>; its storage ABI is not yet reconstructed.
struct UiGraphFloatVector;
enum class UiGraphOrientation : i32;

// The line graph exposes a distinct Unknown-derived identity at whole-object offset 504 on x86.
// Vector arguments remain opaque until their original ownership and storage are recovered.
class UiLineGraph : public ScUnknown {
public:
  virtual void SetGraphOrientation(UiGraphOrientation orientation) = 0;
  virtual void SetSymbolFont(i32 size, u32 first, u32 second, u32 third) = 0;
  virtual void SetSymbolFont(FtFont *font, u32 color) = 0;
  virtual void SetSeriesCount(u32 count) = 0;
  virtual void InsertSeries(u32 index) = 0;
  virtual void RemoveSeries(u32 index) = 0;
  virtual void RemoveAllSeries() = 0;
  virtual void SetSymbolUse(UiGraphUsage symbol, UiGraphUsage line, u32 index) = 0;
  virtual void SetSymbolCharacter(char character, u32 index) = 0;
  virtual void SetSymbolSize(u32 size, u32 index) = 0;
  virtual void SetSeriesColor(u32 color, u32 index) = 0;
  virtual void SetSeriesName(const ScString &name, u32 index) = 0;
  virtual void SetSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) = 0;
  virtual void InsertSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) = 0;
  virtual void RemoveSeriesInfo(u32 index) = 0;
  virtual void InsertCategoryName(const ScString &name, u32 index) = 0;
  virtual void RemoveCategoryName(u32 index) = 0;
  virtual void InsertData(f32 value, u32 series, u32 index) = 0;
  virtual void RemoveData(u32 index, u32 series) = 0;
  virtual void SetData(f32 value, u32 index, u32 series) = 0;
  virtual void SetData(UiGraphFloatVector *values, u32 series) = 0;
  virtual void AppendData(UiGraphFloatVector *values, u32 series) = 0;
  virtual f32 GetHighestAxisValue(UiGraphAxis axis) = 0;
  virtual f32 GetLowestAxisValue(UiGraphAxis axis) = 0;
  virtual void CompleteRedraw(GxBuffer *buffer) = 0;
  virtual void DrawGraphData(GxBuffer *buffer) = 0;
  virtual void LoadDefaultFonts() = 0;
  virtual i32 GetCenterOfDataPoint(u32 index) = 0;
  virtual void DrawSymbol(GxBuffer *buffer, ScPoint *point, u32 series) = 0;
  virtual void DrawLine(GxBuffer *buffer, ScRect *line, u32 series) = 0;
};

class WnWindow;
class ScMessageTarget;
class UiMapFilter;
class GxPaletteView;
class GxRasterSource;

// Original cISC3WinMapCtrl interface through the palette and filter operations.
// Private nominal returns remain qualified.
class UiMapControl : public ScUnknown {
public:
  virtual u8 CreateControl(u32 identifier) = 0;
  virtual u8 CloneControl(u32 identifier, UiMapControl **out) = 0;
  virtual u8 DestroyControl() = 0;
  virtual u8 Prepare() = 0;
  virtual u8 Unprepare() = 0;
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 SetRotation(i32 degrees) = 0;
  virtual i32 GetRotation() = 0;
  virtual u8 SetDrawWindowOutline(bool enabled) = 0;
  virtual u8 IsDrawWindowOutline() = 0;
  virtual u8 SetWindowOutlineColors(u32 first, u32 second) = 0;
  virtual u8 SetDrawImageOutline(bool enabled) = 0;
  virtual u8 IsDrawImageOutline() = 0;
  virtual u8 SetImageOutlineColors(u32 first, u32 second) = 0;
  virtual u32 GetFillColor() = 0;
  virtual void SetFillColor(u32 pixel) = 0;
  virtual u8 SetCoordinateForwarding(u32 flags, bool enabled, ScMessageTarget *target) = 0;
  virtual u32 GetCoordinateForwardingFlags() = 0;
  virtual u8 SetActualMapSizeInCells(u32 width, u32 height) = 0;
  virtual u8 GetActualMapSizeInCells(u32 *width, u32 *height) = 0;
  virtual void DirtyCellRegion(ScRect *area) = 0;
  virtual u8 GetPalette(u32 id, GxPaletteView **out) = 0;
  virtual u8 AddPalette(u32 id, GxPaletteView *palette) = 0;
  virtual u8 ReleasePalette(u32 id) = 0;
  virtual u8 ReleaseAllPalettes() = 0;
  virtual u32 GetFilterCount() = 0;
  virtual u32 GetVisibleFilterCount() = 0;
  virtual UiMapFilter *GetFilter(u32 id) = 0;
  virtual u8 GetFilter(u32 id, UiMapFilter **out) = 0;
  virtual UiMapFilter *GetFilterByIndex(u32 index) = 0;
  virtual u8 ShowFilter(u32 id) = 0;
  virtual u8 ShowAllFilters() = 0;
  virtual u8 HideFilter(u32 id) = 0;
  virtual u8 HideAllFilters() = 0;
  virtual u8 AddFilter(UiMapFilter *filter) = 0;
  virtual u8 AddFilter(GxRasterSource *sprite) = 0;
  virtual u8 AddFilter(u32 id, GxBuffer *buffer) = 0;
  virtual u8 AddFilterFront(UiMapFilter *filter) = 0;
  virtual u8 AddFilterBack(UiMapFilter *filter) = 0;
  virtual u8 ReleaseFilter(u32 id) = 0;
  virtual u8 ReleaseAllFilters() = 0;
};

class GxBuffer;
class GxPaletteView;
struct ScRect;

// SIMUI IID1504; the installed two concrete filter tables agree on the operations and their PC order.
// Nominal byte/Boolean returns of the private visibility and drawing operations remain qualified by binary evidence.
class UiMapFilter : public ScUnknown {
public:
  virtual void SetFilterId(u32 identifier) = 0;
  virtual u32 GetFilterId() = 0;
  virtual u32 GetWidth() = 0;
  virtual u32 GetHeight() = 0;
  virtual u8 SetVisible(bool enabled) = 0;
  virtual u8 IsVisible() = 0;
  virtual u8 SetPalette(GxPaletteView *palette) = 0;
  virtual GxPaletteView *GetPalette() = 0;
  virtual u8 Draw(GxBuffer *destination, ScRect *area, ScRect *clip) = 0;
  virtual u8 CreateBuffer(u32 width, u32 height, GxBuffer **out) = 0;
  virtual u8 CloneFilter(UiMapFilter **out) = 0;
  virtual void SetPostFilter(bool enabled) = 0;
  virtual void SetDestinationCoords(i32 x0, i32 y0, i32 x1, i32 y1, i32 x3, i32 y3) = 0;
  virtual u8 IsPostFilter() const = 0;
  // Retire the concrete filter through its installed deleting virtual slot.
  virtual ~UiMapFilter() = default;
};

struct CtCoord;
class ScStringView;
class GxRasterSource;
class WnWindow;
class ScOccupant;
class CtCity;
class UiMapsViewInterface;
class GxPaletteView;
class FtFont;
class UiQuery;

// Complete observed42-slot IID579 tableAADC8, implemented by the real UiQueryDialog owner.
// Its secondary callback is source+4; this primary interface is not a WnWindow base or a synthesized provider.
// Const getters preserve the actual mutable outputs; remaining private return/reference declarations stay qualified.
// The native base groups the proven same-primary enumerator prefix; historical C++ inheritance remains unproved.
class UiQueryWindow : public CtCityEnumerator {
public:
  virtual u8 InitAtLocation(const CtCoord *location, u32 flags) = 0;
  virtual u8 InitForOccupant(ScOccupant *target, u32 flags) = 0;
  virtual u8 NotifyTarget(CtCity *city, u8 opening) = 0;
  virtual u8 SetLayout(u32 mode) = 0;
  virtual u8 SetArea(ScRect *area) = 0;
  virtual u8 SetExtent(i32 value) = 0;
  virtual u8 SetHeading(ScStringView *text) = 0;
  virtual u8 SetCaptionEnabled(bool value) = 0;
  virtual u8 SetCaption(const ScStringView &text) = 0;
  virtual u8 SetStatusText(const ScStringView &text) = 0;
  virtual u8 SetStatusPair(ScStringView *first, ScStringView *second) = 0;
  virtual u8 SetBackgroundColor(u32 pixel) = 0;
  virtual u8 GetLayout(u32 *out) const = 0;
  virtual u8 GetArea(ScRect *out) const = 0;
  virtual u32 GetExtent() const = 0;
  virtual ScStringView *GetHeading() = 0;
  virtual ScStringView *GetCaption() = 0;
  virtual ScStringView *GetAlternateStatusText() = 0;
  virtual u8 GetCaptionEnabled() = 0;
  virtual u8 GetBackgroundColor(u32 *out) = 0;
  virtual u8 SetPreviewWindow(WnWindow *preview) = 0;
  virtual u8 BuildPreview() = 0;
  virtual u8 QueryPreviewInterface(u32 iid, void **out) const = 0;
  virtual u8 GetPictureArea(ScRect *out) const = 0;
  virtual u8 QuerySectionInterface(u32 iid, void **out) const = 0;
  virtual u8 GetContentArea(ScRect *out) const = 0;
  virtual u8 SetSection(UiQuery *value) = 0;
  virtual u32 GetSectionCount() = 0;
  virtual u8 AddSection(UiQuery *value) = 0;
  virtual u8 FindSectionInterface(u32 *tag, u32 iid, void **out) = 0;
  virtual WnWindow *GetWindow() const = 0;
  virtual ScOccupant *GetTarget() const = 0;
  virtual u8 GetLocation(CtCoord *out) const = 0;
  virtual u8 LayoutOption() = 0;
  virtual u8 AddOption(const ScStringView &caption, FtFont *font, bool checked) = 0;
  virtual u8 IsOptionChecked() = 0;
  // Used effects of original160/6208B SetLabelFlags(u32); its private nominal return remains unresolved.
  virtual void SetLabelFlags(u32 flags) = 0;
  // Retire the concrete query owner through the observed deleting-destructor slot164.
  virtual ~UiQueryWindow() = default;
};

// Catalog text and resource-key properties shared by advisor producers and the building picker.
enum UiCatalogField : u32 {
  kUiCatalogAlreadyPlacedField = 0xc2ebe7d0,
  kUiCatalogNameField = 0x02ebe76b,
  kUiCatalogDescriptionField = 0x02ebe7cb,
  kUiCatalogSecondaryTextField = 0x22f291ae,
  // Optional short heading above the catalog body; landmark rows can fall back to the main text.
  kUiCatalogDetailHeadingField = 0x03091993,
  kUiCatalogResourceTypeField = 0xba64f91e,
  kUiCatalogResourceGroupField = 0xfa64f93a,
  kUiCatalogResourceInstanceField = 0xfa64f957,
};

// Building-picker categories, identified by their installed SC3StringsBldCatalog headings.
enum UiQueryCategory : u32 {
  kUiQueryLandmarks = 0x22e92581,
  kUiQueryRewards = 0x62e92553,
  kUiQueryPowerBuildings = 0x62e925a2,
  kUiQueryBuildings = 0xa2f7d03a,
  kUiQueryWasteBuildings = 0xc2f291a9,
  kUiQueryWaterBuildings = 0xe2e92527,
};

// Portrait resource instances encode the small-image selection separately from the decoded mode bias.
enum UiAdvisorPortraitField : u32 {
  kAdPortraitSmallFlag = 0x1000000,
  kAdPortraitModeBias = 0x400000,
};

// Slide widgets notify their parent of a changed selection through this window-command payload.
enum UiSlideCommand : u32 {
  kUiSlideSelectionChangedCommand = 0xba1ebd09,
};

// Business-deal queries select this view and publish deal names and income into its two columns.
enum AdBusinessDealQueryKey : u32 {
  kAdBusinessDealQuerySelector = 0x42fa5751,
  kAdBusinessDealNameColumn = 0xe2fa5760,
  kAdBusinessDealIncomeColumn = 0x62fa576c,
};

// Detail rows sort into two groups before construction-date and monthly-cost rows.
// Keep these category bits separate from geometry flags and window-message options with the same numeric values.
enum UiQueryRowCategory : u32 {
  kUiQueryPrimaryDetailsCategory = 0x20000,
  kUiQuerySecondaryDetailsCategory = 0x40000,
  kUiQueryConstructionDateCategory = 0x100000,
  kUiQueryMonthlyCostCategory = 0x800000,
};

// Query rows and sections use this mask to participate in every category.
inline constexpr u32 kUiAllQueryCategories = UINT32_MAX;

// Complete observed20-slot IID583 table from SIMUI class584's secondary view, with the original overload order.
// Private nominal bool/BYTE and input pointer/reference spellings are unresolved; retain the proven byte and pointer ABI.
// The same-view enumerator prefix is grouped natively; historical C++ inheritance spelling remains unproved.
class UiQuery : public CtCityEnumerator {
public:
  // Source16/64242 retains the supplied query owner and notifies its occupant, city and layers.
  virtual u8 BindTo(UiQueryWindow *owner) = 0;
  // Source20/64136 delivers the opening/closing message before city-layer enumeration.
  virtual u8 NotifyTarget(CtCity *city, u8 opening) = 0;
  // Source24/643D3 borrows the stored four-byte section identifier at secondary view+36.
  virtual u32 *GetTag() = 0;
  // Source28/641DC copies the pointed-to identifier;36/641EA copies the title interface.
  virtual u8 SetTag(const u32 *tag) = 0;
  // Source32/643CF borrows the embedded title; the caller supplies any combo index to InsertString afterward.
  virtual ScStringView *GetTitle() = 0;
  virtual u8 SetTitle(const ScStringView &title) = 0;
  // MSVC reverses this overload group: rectangle40/6499E, then separate coordinates44/649A9.
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual void SetArea(const ScRect *area) = 0;
  // Source48/649B4 copies the concrete section's stored rectangle into the caller's output.
  virtual void GetArea(ScRect *out) = 0;
  // Source52 returns the borrowed owner window through the source secondary-view adjustment.
  virtual WnWindow *GetWindow() = 0;
  // Source56/8EBF5 borrows the retained dialog field through a folded offset4 getter.
  virtual UiQueryWindow *GetOwner() = 0;
  // Source60 borrows the query owner's target occupant without acquiring a reference.
  virtual ScOccupant *GetTarget() = 0;

  // Source64 forwards its one output pointer and the target's raw AL unchanged, or returns zero without a target.
  virtual u8 GetLocation(CtCoord *out) = 0;
  // Used effects of source68/64695; the private nominal return declaration remains unresolved.
  virtual void SetCategoriesEffects(u32 categories) = 0;
  // Source72/643D7 updates the separate cached query fonts; its private nominal name/return remain qualified.
  virtual u8 SetQueryFonts(FtFont *first, FtFont *second) = 0;
  // Source76 has four stack words/RET16; its final low BYTE selects warning colors and is never dereferenced.
  virtual u8 AddText(const ScStringView *label, const ScStringView *value, u32 categories, u8 warning) = 0;
};

// Producers and map views share these raster identities for terrain, simulation overlays and marker layers.
enum UiMapRasterId : u32 {
  kUiTerrainMapRasterId = 0x2711,
  kUiTrafficMapRasterId = 0x2715,
  kUiZoningMapRasterId = 0x271b,
  kUiAuraMapRasterId = 0x271c,
  kUiCrimeMapRasterId = 0x271d,
  kUiCrimeMarkersRasterId = 0x271e,
  kUiWaterSupplyMapRasterId = 0x2720,
  kUiPowerMapRasterId = 0x2722,
  kUiPopulationMapRasterId = 0x2724,
  kUiLandValueMapRasterId = 0x2726,
  kUiFlammabilityMapRasterId = 0x272c,
  kUiFlammabilityMarkersRasterId = 0x272d,
  kUiAirPollutionMapRasterId = 0x2731,
  kUiWaterPollutionMapRasterId = 0x2732,
  kUiGarbagePollutionMapRasterId = 0x2733,
  kUiRadiationMapRasterId = 0x2735,
};

// IID59A is the maps-and-graphs window's complete primary interface through city enumeration.
// The declaration order preserves the installed MSVC overload groups and their original slots.
class UiRasterQuery : public ScUnknown {
public:
  virtual u8 Init() = 0;
  virtual bool DoesMapViewExist() = 0;
  virtual u32 GetCurrentViewId() = 0;
  virtual u32 GetDefaultViewId() = 0;
  // Effect-only spelling of original44424: store the const DWORD input. The nominal source return remains unresolved.
  virtual void SetDefaultViewId(const u32 &identifier) = 0;
  virtual WnWindow *GetWindow() const = 0;
  virtual u8 SetCaption(const ScStringView &caption) = 0;
  virtual u8 GetCaption(ScStringView *out) const = 0;
  virtual u8 SetFrameArea(const ScRect &area) = 0;
  virtual u8 GetFrameArea(ScRect *out) const = 0;
  virtual u8 ShowView(u32 identifier) = 0;
  virtual u8 ShowView(UiMapsViewInterface *view) = 0;
  virtual u8 GetView(const u32 &identifier, u32 iid, void **out) = 0;
  virtual UiMapsViewInterface *GetView(const u32 &identifier) = 0;
  virtual u8 AddPalette(u32 identifier, GxPaletteView *palette) = 0;
  virtual GxPaletteView *GetPalette(u32 identifier) = 0;
  virtual u32 GetPaletteCount() const = 0;
  virtual GxPaletteView *GetPaletteByIndex(u32 index) = 0;
  virtual u8 AddRaster(GxRasterSource *source) = 0;
  virtual GxRasterSource *FindRaster(u32 kind) = 0;
  virtual u32 GetRasterCount() const = 0;
  virtual GxRasterSource *GetRasterByIndex(u32 index) = 0;
  virtual i32 IsReady() = 0;
  virtual u8 ReleaseAllPalettes() = 0;
  virtual u8 ReleaseAllRasters() = 0;
  virtual u8 RefreshData(CtCity *city) = 0;
  virtual u8 EnumCityItems(CtCity *city, bool enabled) = 0;
  virtual ~UiRasterQuery() = default;
};

class ScStringView;
class UiQueryRow;
class UiQueryIterator;
class UiQueryProperty;
class ScVariant;

// Native grouping of the verified same-pointer Unknown prefix and reached SIMUI query-collection operations.
// This is a subset, not a foreign vtable overlay; private formal names and bool-versus-BYTE returns remain unresolved.
class UiQueryRows : public ScUnknown {
public:
  virtual u8 CreateIterator(UiQueryIterator **out) = 0; // Original12,BFBC publishes a zero-reference iterator.
  virtual u32 GetCount() const = 0;                     // Original16,BFFC reads the live row count.
  // Used effects of original20/C0E1; the private nominal return remains unresolved.
  virtual void SetSortPropertyEffects(u32 key) = 0;
  virtual u8 AppendRow(UiQueryRow *row) = 0; // Original24,C006 retains the inserted row and returns AL1.
  virtual UiQueryRow *CreateRow() const = 0; // Original28,C067/C62B returns a new zero-reference20-byte row.
  virtual u8 Clear() = 0;                    // Original32,C099 releases rows in their current order.
};

#ifdef GetObject
#undef GetObject
#endif
// Reached row operations from tableA27C8; text values are copied and unsigned words use the real variant type7.
// Keep raw byte carriers until private nominal declarations are established; real providers live in query_records.cpp.
class UiQueryRow : public ScUnknown {
public:
  virtual u8 GetProperty(u32 key, UiQueryProperty **out) = 0;  // Original12,C2C8; borrowed output, untouched on miss.
  virtual u8 SetProperty(u32 key, UiQueryProperty *value) = 0; // Original16,C2F8; retains even a rejected duplicate.
  virtual UiQueryProperty *CreateProperty() const = 0;         // Original20,C329; zero-reference12-byte value holder.
  virtual u8 SetText(u32 key, ScStringView *value) = 0; // Original24,C3C5 delegates to the AL1 property insertion.
  virtual u8 SetOptionalText(u32 key, ScStringView *value) = 0; // Original28,C35B refuses null text.
  virtual u8 SetObject(u32 key, ScUnknown *value) = 0;          // Original32,C420 refuses null objects.
  virtual u8 SetU32(u32 key, u32 value) = 0;  // Original36,C48A constructs the actual unsigned variant.
  virtual ScStringView *GetText(u32 key) = 0; // Original40,C4E7 allocates a zero-reference String copy, or null.
  virtual ScUnknown *GetObject(u32 key) = 0;  // Original44,C56F borrows the stored object, or null.
  virtual u32 GetU32(u32 key) = 0;            // Original48,C5AE returns zero for an absent property/value.
  virtual u32 GetType(u32 key) = 0;           // Original52,C5EF returns zero for an absent property/value.
};

// Original iteratorA2780 borrows rows while retaining its actual collection owner.
class UiQueryIterator : public ScUnknown {
public:
  virtual UiQueryRow *GetFirst() = 0;
  virtual UiQueryRow *GetCurrent() const = 0;
  virtual UiQueryRow *GetNext() = 0;
  virtual UiQueryRow *GetAt(u32 index) = 0;
};

// Original holderA283C owns one variant; GetValue publishes a borrowed pointer even when it is null.
class UiQueryProperty : public ScUnknown {
public:
  virtual u8 SetValue(ScVariant *value) = 0;
  virtual u32 GetValue(ScVariant **out) = 0; // C783 explicitly returns the normalized full EAX value.
};

class WnWindow;

// Save the window's left/top pair in the application's real internet preferences, using the original two arguments.
// Source checks only kind<15; callers must keep negative kinds outside the defined array-index domain.
u8 UiSaveWindowPosition(WnWindow *window, i32 kind);
u8 UiLoadWindowPosition(WnWindow *window, i32 kind);

#endif
