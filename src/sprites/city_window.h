#ifndef SC3_SPRITES_CITY_WINDOW_H
#define SC3_SPRITES_CITY_WINDOW_H

#include "application/api.h"
#include "city/api.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"
#include "sprites/sp_tick_manager.h"
#include "ui/api.h"
#include "window/api.h"

class GeAttrib;
class ScOccupant;
class SpDefView;
class SpSprite;

// The three discarded setter returns are also provisional. MSVC reverses each overload group in table.
class SpCitySpritePreview : public ScUnknown {
public:
  virtual bool SetCitySprite(SpSprite *sprite) = 0;
  virtual bool SetCitySprite(SpDefView *definition) = 0;
  virtual bool SetCitySprite(GeAttrib *attributes) = 0;
  virtual bool SetCitySprite(GeAttrib *attributes, u32 layer) = 0;
  virtual bool SetCitySprite(ScOccupant *occupant) = 0;
  virtual bool SetCitySprite(ScOccupant *occupant, u32 layer) = 0;
  virtual bool SetCompass(i32 compass) = 0;
  virtual bool SetZoom(i32 zoom) = 0;
  virtual i32 GetCompass() const = 0;
  virtual i32 GetZoom() const = 0;
  virtual bool SetSpriteZoomToFitInWindowArea() = 0;
  virtual bool SetWindowSizeToSpriteSize() = 0;
  virtual const ScRect *GetArea() const = 0;
  // MSVC reverses these overloads: the rectangle is slot64 and four coordinates occupy slot68.
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual void SetArea(const ScRect &rectangle) = 0;
  virtual bool SetStretch(bool stretch, bool preserve_aspect) = 0;
  virtual void SetFillColor(u32 color) = 0;
  virtual WnWindow *AsIGZWin() = 0;
  virtual SpSprite *GetAssociatedCitySprite() = 0;
  virtual bool GetSpriteHeightToWidthRatio(f32 *ratio) const = 0;
  // These three setters' source return types are not established; machine return registers are incidental.
  virtual void SetDrawSunkenRect(bool enabled) = 0;
  virtual void SetSunkenRectColors(u32 first, u32 second) = 0;
  virtual void SetKeepWindowSize(bool enabled) = 0;
};

// The preview identity follows the 164-byte Windows window base. Rendering and initialization remain unrecovered.
class SpCitySpriteWindow : public WnSpriteRootWidget, public SpCitySpritePreview {
public:
  SpCitySpriteWindow();
  ~SpCitySpriteWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool SetCitySprite(SpSprite *sprite) override;
  bool SetCitySprite(SpDefView *definition) override;
  bool SetCitySprite(GeAttrib *attributes) override;
  bool SetCitySprite(GeAttrib *attributes, u32 layer) override;
  bool SetCitySprite(ScOccupant *occupant) override;
  bool SetCitySprite(ScOccupant *occupant, u32 layer) override;
  bool SetCompass(i32 compass) override;
  bool SetZoom(i32 zoom) override;
  i32 GetCompass() const override;
  i32 GetZoom() const override;
  bool SetSpriteZoomToFitInWindowArea() override;
  bool SetWindowSizeToSpriteSize() override;
  const ScRect *GetArea() const override;
  void SetArea(const ScRect &rectangle) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool SetStretch(bool stretch, bool preserve_aspect) override;
  void SetFillColor(u32 color) override;
  WnWindow *AsIGZWin() override;
  SpSprite *GetAssociatedCitySprite() override;
  bool GetSpriteHeightToWidthRatio(f32 *ratio) const override;
  void SetDrawSunkenRect(bool enabled) override;
  void SetSunkenRectColors(u32 first, u32 second) override;
  void SetKeepWindowSize(bool enabled) override;
  bool Init() override;
  bool GZPaint() override;
  bool Shutdown() override;
  void SetAreaToDrawTo() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;

private:
  u8 SetupBuffer();
  // The original helper's callers ignore its incidental return value.
  void SetupBltRect();
  bool stretch, preserve_aspect, preview_dirty;
  u32 preview_fill, unknown176;
  bool sunken;
  ScRect blit_rect;
  u32 sunken_first, sunken_second;
  bool keep_window_size;
  GxBuffer *preview_buffer;
  SpSprite *sprite;
  i32 zoom, compass;
  ScRect destination_rect, source_rect;
};
ScUnknown *SpCreateCitySpriteWindow();

class SpView;
class SpCellSelection;
class ScCommandFactory;
class UiLayerDialogInterface;
struct WnHint;

// Original factory returns the unretained window identity.
ScUnknown *SpCreateCityWindow();

// SIMSPR's original cSC3CityChangeSender base owns the change-source identity and retained receivers.
class CtChangeSender : public CtChangeSource {
public:
  explicit CtChangeSender(u32 sender);
  virtual ~CtChangeSender();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetID() const override;
  bool AddReceiver(CtChangeReceiver *receiver, u32 *sender) override;
  bool RemoveReceiver(CtChangeReceiver *receiver) override;
  bool RemoveAll() override;
  void SetEnabled(u8 enabled) override;
  u8 GetEnabled() const override;
  bool NotifyAll() override;
  bool Notify(u32 x, u32 z) override;
  bool Notify(u32 left, u32 top, u32 right, u32 bottom) override;

protected:
  u32 senderId, references;
  u8 updatesEnabled;
  ScArray<CtChangeReceiver *> receivers;
};

// The installed cursor owns distinct cursor, animation and change-sender identities. The ellipse operation
// remains incomplete; the window initializer constructs the cursor and retains its interface.
class SpCityCursor : public UiCityCursor, public SpTickable, public CtChangeSender {
public:
  SpCityCursor();
  ~SpCityCursor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  bool Init(CtCity *city) override;
  bool Shutdown() override;
  u32 CellCountX() override;
  u32 CellCountZ() override;
  bool InBounds(const CtCoord &position) const override;
  bool InBounds(const GeBounds &bounds) const override;
  void SetColor(const CtCoord &position, u32 color) override;
  void SetColor(const GeBounds &bounds, u32 color) override;
  void GetColor(const CtCoord &position, u32 *color) override;
  void SetPalette(u32 palette) override;
  void SetDefaultColor(u32 palette, u32 color) override;
  void GetDefaultColor(u32 *palette, u32 *color) override;
  bool Clear(bool update) override;
  void SetCell(const CtCoord &position, u32 color) override;
  void SetLine(const CtCoord &first, const CtCoord &last, u32 color) override;
  void SetRect(const GeBounds &bounds, u32 color) override;
  bool SetEllipse(const CtCoord &first, const CtCoord &last, u32 color) override;
  void SetCellList(SpSelection *first, u32 firstColor, SpSelection *second, u32 secondColor) override;
  bool UpdateRegion(const GeBounds &bounds) override;
  bool GetEnumerator(SpSelection **out) override;
private:
  void SetCell(i32 x, i32 z, u32 color); // Private nominal return is unresolved; callers use its effects.
  void SetBounds(i32 left, i32 top, i32 right, i32 bottom, u32 color); // Private nominal return is unresolved.
  void Clear(); // Private nominal return is unresolved; callers use its effects.
  u8 initialized, empty;
  u32 palette, defaultColor, unknown48;
  CtCity *city;
  GeBounds bounds, changedBounds;
  SpCellSelection *selection;
  ScCellMap<u8> *cells;
};

// SIMSPR's city-view identity precedes its window identity. Private nominal return declarations remain qualified below.
class SpCityWindow : public SpCityWindowView, public WnSpriteRootWidget {
public:
  SpCityWindow();
  ~SpCityWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  void FocusCameraOn(u32 x, u32 z, bool update) override;
  void ZoomIn(bool update) override;
  void ZoomOut(bool update) override;
  void RotateCameraCW(bool update) override;
  void RotateCameraCCW(bool update) override;
  void Translate(f32 x, f32 y, bool update) override;
  void SetEdgeScroll(bool enabled) override;
  void SetMouseScroll(i16 x, i16 y, bool enabled) override;
  void UpdateScroll(i16 x, i16 y) override;
  void SetCellCursorSize(u32 width, u32 height) override;
  bool CellCursorVisible() override;
  void SetCellCursorVisible(bool visible) override;
  void SetCostDisplay(bool visible, i32 first, i32 second) override;
  void EnableCostDisplay(bool enabled) override;
  bool SetActiveHandler(SpViewHandler *handler) override;
  void SetActiveCursor(u32 id) override;
  bool DefActiveCursor(u32 *id) override;
  bool DefOnKeyDown(u32 key, u32 modifiers) override;
  bool DefOnKeyUp(u32 key, u32 modifiers) override;
  bool DefOnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool DefOnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool DefOnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool DefOnMouseUpR(i32 x, i32 y, u32 modifiers) override;
  bool DefOnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool DefOnHotKey(u32 key, u32 modifiers, u32 command) override;
  void CameraGoHome() override;
  SpManagerView *GetViewManager() override;
  UiCityCursor *GetCellCursorMap() const override;
  void AnimationPause(bool pause) override;
  SpViewInterface *GetCurrentView() override;
  SpIsoView *GetIsoView() override;
  bool SetCurrentView(SpViewInterface *view) override;
  bool SwitchHandler(i32 mode, SpViewHandler *handler) override;

  // Installed private primary slots 41..46; nominal return spellings remain unidentified.
  virtual void PreCityChange();
  virtual void PostCityChange();
  virtual void GetCameraHint(ScRect *first, ScRect *second);
  virtual void SetZoom(i32 zoom, i32 rotation, bool update);
  virtual void SetViewParam(u32 key, u32 value, bool update);
  virtual void UpdateDataView();

  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  bool GZOnMouseEnter(WnWindow *window) override;
  bool GZOnMouseExit(WnWindow *window) override;
  bool OnCommand(u32 kind, u32 command) override;

private:
  void OnMouseExit();         // The private nominal return is unresolved; reached callers use its effects only.
  void UpdateScrollRegions(); // The private nominal return is unresolved; reached callers use its effects only.
  void DoSetBoxXPosition();   // The private nominal return is unresolved; reached callers use its effects only.
  void DoSetBoxXRotation();   // The private nominal return is unresolved; reached callers use its effects only.
  void DoSetBoxXZoom();       // The private nominal return is unresolved; reached callers use its effects only.
  void ResetScrollRates();    // The private nominal return is unresolved; reached callers use its effects only.
  void SaveScrollMargin();    // The private nominal return is unresolved; reached callers use its effects only.
  void InitWinLayers(UiLayerDialogInterface *dialog);
  void UpdateWinLayers(UiLayerDialogInterface *dialog);
  void PaintScroll(); // The private nominal return is unresolved; reached callers use its effects only.

  // Installed x86 offsets follow the 164-byte window base; unknown fields retain their observed widths.
  ScFramework *framework; // Borrowed module service.
  WnWindowManager *windowManager; // Borrowed module service.
  ApGameInterface *application; // Borrowed application identity.
  ApBoxX *boxX; // Borrowed from the application.
  SpViewInterface *currentView; // Borrowed selection; initially aliases the owned isoView.
  SpView *isoView; // Retained by Init, shut down and released by Shutdown.
  UiCityCursor *cellCursor; // Retained by Init, shut down and released by Shutdown.
  GxBuffer *viewportBuffer; // Borrowed active screen buffer; cleared without Release.
  u32 unknown200[4];
  ScRect viewportBounds, screenBufferBounds;
  bool cellCursorVisible;
  u32 cursorWidth, cursorHeight, defaultCursor, activeCursor;
  bool showCellCoordinates;
  FtFont *font; // Retained resource font; released by Shutdown.
  i32 unknown276, unknown280;
  bool costShown;
  u32 costDisplayDisableCount;
  u32 costColors[2];
  i8 costMode;
  i32 costValue;
  ScString costText;
  ScRect costRectangle;
  u32 unknown344[4];
  WnHint *costHint; // Owned allocation; deleted by Shutdown.
  u32 unknown364, unknown368;
  bool simulationPausedByView, rightFocusPending, rightDragMoved, edgeScrollEnabled;
  ScRect scrollCenter, scrollLeft, scrollTop, scrollRight, scrollBottom;
  f32 scrollRate, scrollRates[5];
  bool unknown480, edgeScrollFlags[4], edgeStateChanged, mouseScrolling;
  u8 unknown487[4];
  i16 mouseScrollX, mouseScrollY, unknown496;
  f32 mouseScrollDeltaX, mouseScrollDeltaY;
  GxBuffer *anchorCursorImage; // Owned resource reference; released by Shutdown.
  bool leftButtonDown, rightButtonDown;
  i32 lastMouseX, lastMouseY;
  u32 lastMouseModifiers;
  WnKeyboard *keyboardState; // Borrowed from the window manager.
  u8 unknown532[7];
  bool inputReentrant, unknown540, unknown541;
  i32 handlerMode, previousHandlerMode;
  SpViewHandler *activeHandler, *previousHandler; // Retained references move between slots during tool switches.
  ScCommandFactory *commandFactory; // Borrowed service; the constructor releases its temporary query reference.
  bool unknown564;
};

struct CtCoord;

// Selection enumerator IID62C0838E, produced by SIMSPR and SIMNTWRK. These reached operations share Unknown identity.
class SpSelection : public ScUnknown {
public:
  virtual u8 Next(u32 maximum, CtCoord *out, u32 *count) = 0; // Original12; SIMSPR5B9A1.
  virtual u8 Skip(u32 count) = 0;                             // Original16; SIMSPR5B9F4.
  virtual u8 Reset() = 0;                                     // Original20; SIMSPR5BA00.
  virtual u8 Clone(SpSelection **out) = 0;                    // Original24; SIMSPR5BA07.
};

// Concrete SIMSPR coordinate enumerator at table10068E48. The private historical class spelling is unproved;
// native storage uses the project's trivial array rather than exposing the original STL vector's pointer layout.
class SpCellSelection : public SpSelection {
public:
  SpCellSelection();
  SpCellSelection(const SpCellSelection &source);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Next(u32 maximum, CtCoord *out, u32 *count) override;
  u8 Skip(u32 count) override;
  u8 Reset() override;
  u8 Clone(SpSelection **out) override;
  virtual bool GetBounds(GeBounds *out);
  virtual u8 AppendFrom(SpSelection *source);
  virtual u8 SortAndCompact();
  virtual ~SpCellSelection();
  void Append(const CtCoord &position);

  ScArray<CtCoord> points;
  u32 cursor, references;

private:
  SpCellSelection &operator=(const SpCellSelection &) = delete;
};

class CtZoneLayer;

// Original cSC3EmptyTest and its fieldless view specializations share the same nonvirtual storage release.
// They have no owned members or virtual destructor; the surface filter borrows its zone layer.
class SpEmptyTest : public OcIteratorTest {
public:
  SpEmptyTest();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

  u32 references;
};

class SpSurfaceTest : public SpEmptyTest {
public:
  explicit SpSurfaceTest(CtZoneLayer *zone);
  bool Test(ScOccupant *occupant) override;
  // Reached six-Boolean setter effects; its unused original nominal return type remains unrecovered.
  virtual void SetTest(bool flora, bool transport, bool civic, bool zoned, bool unzoned, bool extra);

  bool show_flora, show_transport, show_civic, show_zoned, show_unzoned, extra;
  CtZoneLayer *zone;
};

class SpSubwayTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

class SpPipeTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

class SpFlammabilityTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

class SpCrimeTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

class SpTrafficTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

class SpPowerTest : public SpEmptyTest {
public:
  bool Test(ScOccupant *occupant) override;
};

#endif
