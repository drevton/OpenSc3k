#ifndef SC3_BUILDINGS_ARCHITECT_API_H
#define SC3_BUILDINGS_ARCHITECT_API_H

#include <stdio.h>

#include "buildings/architect/bd_geometry.h"
#include "buildings/architect/image.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "graphics/api.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"

#ifndef BD_FASTCALL
#if defined(_WIN32)
#define BD_FASTCALL __fastcall
#else
#define BD_FASTCALL
#endif
#endif

// Architect menu, toolbar, asset palette and minimap controls follow their construction and dispatch.
enum BdWorkspaceControlId : u32 {
  kBdToolPanelChildId = 0x5abcb33,
  kBdRenderBuildingListButtonId = 0x45c78d0a,
  kBdMainMenuCloseButtonId = 0xa2fbb0b3,
  kBdMainMenuBuildingInfoButtonId = 0x63fa2883,
  kBdMainMenuNewBuildingButtonId = 0xa3cca1c0,
  kBdMainMenuOpenBuildingButtonId = 0xa3cca1c1,
  kBdMainMenuExitButtonId = 0xe2fba4b4,
  kBdMainMenuWindowId = 0x63cc678e,
  kBdMainMenuOpenBuildingLabelId = 0x25bd4213,
  kBdMainMenuNewBuildingLabelId = 0x65bd4211,
  kBdMainMenuCloseLabelId = 0x35bd4215,
  kBdMainMenuTitleLabelId = 0x75c2d16c,
  kBdMainMenuCopyrightLabelId = 0x75c2c8bf,
  kBdMainMenuVersionLabelId = 0x75c2c8be,
  kBdToolGroupTabId = 0xa434d42c,
  kBdToolItemScrollbarId = 0x45b91e78,
  kBdMiniMapCanvasId = 0x641cf25d,
  kBdMiniMapRotateNegativeId = 0x641cf259,
  kBdMiniMapRotatePositiveId = 0x641cf25a,
  kBdMiniMapZoomInId = 0x641cf25b,
  kBdMiniMapZoomOutId = 0x641cf25c,
  kBdAssetGroupIdBase = 0xa429fbb9,
  kBdShapeAssetGroupId = 0xa429fbba,
  kBdMaterialAssetGroupId = 0xa429fbbb,
  kBdCompositionAssetGroupId = 0xa429fbbc,
  kBdPropAssetGroupId = 0xa429fbbd,
  kBdToolStripIdBase = 0x15b1a3a2,
  kBdShapeItemId = 0x644bb2b1,
  kBdWallMaterialItemId = 0x644bb2b2,
  kBdGroundMaterialItemId = 0x644bb2b3,
  kBdRooftopMaterialItemId = 0x644bb2b4,
  kBdColorMaterialItemId = 0x644bb2b5,
  kBdCustomMaterialItemId = 0x644bb2b6,
  kBdTraditionalCompositionItemId = 0x644bb2b7,
  kBdContemporaryCompositionItemId = 0x644bb2b8,
  kBdCustomCompositionItemId = 0x644bb2b9,
  kBdCustomPropItemId = 0x644bb2ba,
  kBdArchitecturePropItemId = 0x644bb2bc,
  kBdDecorationPropItemId = 0x644bb2bd,
  kBdPlazaStreetPropItemId = 0x644bb2be,
  kBdYardPropItemId = 0x644bb2bf,
  kBdFloraPropItemId = 0x644bb2c0,
  kBdIndustrialPropItemId = 0x644bb2c1,
  kBdRooftopPropItemId = 0x644bb2c2,
  kBdVehiclePropItemId = 0x644bb2c3,
  kBdConstructGroupButtonId = 0xe4161b03,
  kBdPaintGroupButtonId = 0xe4161b04,
  kBdCompositionGroupButtonId = 0xe4161b05,
  kBdPropGroupButtonId = 0xe4161b38,
  kBdFileGroupButtonId = 0xe4161b06,
  kBdBlockToolButtonId = 0xe4161b07,
  kBdLineToolButtonId = 0xe4161b08,
  kBdPlaneToolButtonId = 0xe4161b09,
  kBdFillToolButtonId = 0xe4161b0a,
  kBdPaintbrushToolButtonId = 0xe4161b0b,
  kBdCompositionToolButtonId = 0xe4161b0c,
  kBdPropToolButtonId = 0xe4161b39,
  kBdSaveToolButtonId = 0xe4161b0d,
  kBdSaveAsToolButtonId = 0xe4161b0e,
  kBdLoadToolButtonId = 0xe4161b0f,
  kBdNewToolButtonId = 0xe4161b10,
  kBdExitToolButtonId = 0xe4161b11,
  kBdConstructPaletteWindowId = 0xe4161b12,
  kBdMaterialPaletteWindowId = 0xe4161b3a,
  kBdCompositionPaletteWindowId = 0xe4161b14,
  kBdPropPaletteWindowId = 0xe4161b3b,
  kBdToolMenuDecorationId = 0xc40f6c3f,
  kBdEraseToolButtonId = 0x75b3d3c1,
  kBdSelectToolButtonId = 0x55b3d3bc,
  kBdUndoToolButtonId = 0x75b3d3bf,
  kBdRedoToolButtonId = 0x65b3d3bb,
  kBdMiniMapWindowId = 0x45b12598,
  kBdToolMenuWindowId = 0x55b2c0ec,
};

// Cursor identities selected by the architect tool-mode dispatcher.
enum BdCursorId : u32 {
  kBdEraseCursorId = 0x237a84e9,
  kBdSelectCursorId = 0xa37a855b,
  kBdBlockCursorId = 0x237a8577,
  kBdLineCursorId = 0x237a8578,
  kBdPlaneCursorId = 0x237a8579,
  kBdPaintbrushCursorId = 0x237a856b,
  kBdFillCursorId = 0xc37a856f,
  kBdCompositionCursorId = 0x237a857a,
  kBdPropCursorId = 0x485d108,
  kBdHandleCursorId = 0x637a8564,
};

// Resource groups shared by the architect workspace and its dialogs.
enum BdImageGroup : u32 {
  kBdWorkspaceImageGroup = 0xc41602b3,
  kBdDialogImageGroup = 0x6410b233,
};

// Tool modes forwarded by the palette and retained independently from the editor tool category.
enum BdToolMode : i32 {
  kBdToolNone = 0,
  kBdToolUndo = 1,
  kBdToolRedo = 2,
  kBdToolErase = 3,
  kBdToolSelect = 4,
  kBdToolBlock = 5,
  kBdToolLine = 6,
  kBdToolPlane = 7,
  kBdToolPaintbrush = 8,
  kBdToolFill = 9,
  kBdToolComposition = 10,
  kBdToolProp = 11,
  kBdToolRestorePrevious = 21,
};

// Editor accelerator commands select navigation, selection movement and document operations.
enum BdEditorCommand : i32 {
  kBdEditorCommandZoomIn = 1,
  kBdEditorCommandZoomOut = 2,
  kBdEditorCommandRotatePositive = 5,
  kBdEditorCommandRotateNegative = 6,
  kBdEditorCommandLowerSelection = 23,
  kBdEditorCommandRaiseSelection = 25,
  kBdEditorCommandLowerSelectionEight = 27,
  kBdEditorCommandRaiseSelectionEight = 29,
  kBdEditorCommandNudgeNegativeX = 31,
  kBdEditorCommandNudgeNegativeXNegativeY = 32,
  kBdEditorCommandNudgeNegativeY = 33,
  kBdEditorCommandNudgeNegativeXPositiveY = 34,
  kBdEditorCommandNudgePositiveXNegativeY = 35,
  kBdEditorCommandNudgePositiveY = 36,
  kBdEditorCommandNudgePositiveXPositiveY = 37,
  kBdEditorCommandNudgePositiveX = 38,
  kBdEditorCommandNudgePositiveZ = 39,
  kBdEditorCommandNudgeNegativeZ = 40,
  kBdEditorCommandCycleToolMode = 42,
  kBdEditorCommandEraseTool = 48,
  kBdEditorCommandSelectTool = 50,
  kBdEditorCommandUndo = 52,
  kBdEditorCommandUndoAlternate = 53,
  kBdEditorCommandRedo = 54,
  kBdEditorCommandRedoAlternate = 55,
  kBdEditorCommandCopySelection = 61,
  kBdEditorCommandCutSelection = 62,
  kBdEditorCommandPasteSelection = 63,
  kBdEditorCommandSave = 72,
  kBdEditorCommandSaveAs = 73,
  kBdEditorCommandOpen = 74,
  kBdEditorCommandExit = 75,
  kBdEditorCommandSaveAlternate = 87,
  kBdEditorCommandSaveAsAlternate = 88,
  kBdEditorCommandOpenAlternate = 89,
  kBdEditorCommandExitAlternate = 90,
  kBdEditorCommandZoomInAlternate = 101,
  kBdEditorCommandZoomOutAlternate = 102,
  kBdEditorCommandResetView = 105,
  kBdEditorCommandResetViewAlternate = 107,
  kBdEditorCommandToggleEdges = 114,
  kBdEditorCommandToggleViewHandles = 115,
  kBdEditorCommandToggleEmptyObjects = 117,
};

// Window-protocol notifications shared by the architect workspace, editor and minimap.
enum BdWindowMessage : u32 {
  kWnMsg_ArchitectToolItemSelected = 0x9a27e1c0,
  kWnMsg_ArchitectToolGroupSelectionChanged = 0x743b6f72,
  kWnMsg_ArchitectShowMenu = 0x63cc6991,
  kWnMsg_ArchitectShowMenuAlternate = 0x63cc6992,
  kWnMsg_ArchitectReturnToEditor = 0x63cc699d,
  kWnMsg_ArchitectReturnToEditorAlternate = 0x63cc699e,
  // Unresolved architect ScMessageServer subscriptions: 24..29 target BdWorkspace; 30..31 target BdEditor.
  // Their handlers delegate these IDs to WnArchitectRootWidget; command purposes remain unestablished.
  kWnMsg_Unresolved24 = 0x63cc6990,
  kWnMsg_Unresolved25 = 0x63cc6997,
  kWnMsg_Unresolved26 = 0x63cc6998,
  kWnMsg_Unresolved27 = 0x63cc6999,
  kWnMsg_Unresolved28 = 0x63cc699b,
  kWnMsg_Unresolved29 = 0x63cc699c,
  kWnMsg_Unresolved30 = 0x63cc69ae,
  kWnMsg_Unresolved31 = 0x63cc69af,
  kWnMsg_ArchitectQueryClose = 0x63cc698e,
  kWnMsg_ArchitectEnterEditor = 0x63cc698f,
  kWnMsg_ArchitectNewBuilding = 0x63cc6994,
  kWnMsg_ArchitectOpenBuilding = 0x63cc6995,
  kWnMsg_ArchitectSaveBuilding = 0x63cc6996,
  kWnMsg_ArchitectBuildingInfo = 0x63cc699a,
  kWnMsg_ArchitectZoomIn = 0x63cc699f,
  kWnMsg_ArchitectZoomOut = 0x63cc69a0,
  kWnMsg_ArchitectRotateViewNegative = 0x63cc69a1,
  kWnMsg_ArchitectRotateViewPositive = 0x63cc69a2,
  kWnMsg_ArchitectSetTool = 0x63cc69a7,
  kWnMsg_ArchitectSetAsset = 0x63cc69a8,
  kWnMsg_ArchitectSetBrush = 0x63cc69a9,
  kWnMsg_ArchitectPanView = 0x63cc69ab,
  kWnMsg_ArchitectGridChanged = 0x63cc69ac,
  kWnMsg_ArchitectInvalidateGrid = 0x63cc69ad,
  kWnMsg_ArchitectRotateSelectionNegative = 0x63cc69b0,
  kWnMsg_ArchitectRotateSelectionPositive = 0x63cc69b1,
  kWnMsg_ArchitectViewModeChanged = 0x63cc69b2,
  kWnMsg_ArchitectSelectedCellChanged = 0x63cc69b3,
  kWnMsg_ArchitectSelectionBoundsChanged = 0x63cc69b4,
  kWnMsg_ArchitectProjectionBoundsChanged = 0x63cc69b5,
  kWnMsg_ArchitectBrushPicked = 0x63cc69b6,
  kWnMsg_ArchitectForwardInput = 0x63cc69b7,
  kWnMsg_ArchitectRebuildToolPanel = 0x63cc69b9,
};

// Command words within ArchitectForwardInput; these are distinct from the outer message type.
enum BdInputCommand : u32 {
  kBdCommandSelectionMode = 0x63cc69a6,
  kBdCommandToolMode = 0x63cc69a7,
  kBdCommandAsset = 1674340776,
};

extern u32 BdCrcTable[256];
// Complete initialization effects; the original unused private return declaration remains unresolved.
u32 BdInitializeCrc();
u32 BD_FASTCALL BdHashImage(const u8 *pixels, i32 width, i32 height, i32 pitch);

struct BdFace {
  i8 flags, vertex[4];
  u8 attributes[4];
};

struct BdNeighbor {
  BdPoint3 offset;
  u32 vertices;
};

extern const BdFace BdCellFaces[16][8];
extern const i8 BdCellFaceOrder[16][4][6];
extern const u8 BdCellRotations[4][12];
extern const u8 BdCellAdjacentVertices[6][12];
extern const BdNeighbor BdCellNeighbors[6];

// The linked reader keeps its real C stream, logical position and bounded8K read window.
struct BdCodecFile {
  FILE *stream;
  i32 position, length;
  u8 *read_ptr, *read_end;
  u32 write_mode, field_018;
  u8 buffer[8192];
};

struct BdCodecState {
  u32 signature, length, field_008, field_00C;
  BdCodecFile *file;
  u32 field_014;
};

extern u32 BdCodecCurrent;
void *BD_STDCALL BdAllocateCodec(i32 length);
i32 BD_STDCALL BdReadCodecBytes(BdCodecFile *file, u8 *out, i32 length);
i32 BD_STDCALL BdSeekCodec(BdCodecFile *file, i32 position);
u32 BdCodecValue(const u8 *bytes, u32 count);
i32 BD_STDCALL BdProbeBmp(BdCodecFile *file);
i32 BD_STDCALL BdProbeTga(BdCodecFile *file);
BdCodecInfo *BD_STDCALL BdReadBmpInfo(BdCodecState *state, i32 index);
BdCodecInfo *BD_STDCALL BdReadTgaInfo(BdCodecState *state, i32 index);
i32 BdReadCodecInput(
    BdCodecFile *file, u8 *buffer, u32 capacity, u32 offset, u32 requested, u32 required, u32 *available);
bool BdHasCodecOutput(const u8 *out, const u8 *end, u32 length);
i32 BD_STDCALL BdReadTgaPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
i32 BD_STDCALL BdReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
u32 BD_STDCALL BdWriteCodecBytes(BdCodecFile *file, const void *input, u32 length);
void BdPutCodecValue(u8 *out, u32 value, u32 count);
i32 BdPackTgaRow(const u8 *source, u8 *out, i32 width, i32 depth, const u8 *map, i32 compressed);
i32 BD_STDCALL BdWriteTgaPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
i32 BD_STDCALL BdWriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
BdCodecDescriptor *BdDescribeNilCodec();
BdCodecDescriptor *BdDescribeBmpCodec();
BdCodecDescriptor *BdDescribeTgaCodec();
i32 BD_STDCALL BdSelectCodecForFile(const char *path, BdCodecFile *file);
i32 BD_STDCALL BdOpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *path);
i32 BD_STDCALL BdOpenTgaReader(BdCodecState **out, BdCodecFile *file, const char *path);
i32 BD_STDCALL BdCreateCodecWriterState(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
i32 BD_STDCALL BdCloseCodecStream(BdCodecState *state);
i32 BdCodecCount();

// Packed ARGB receiver used by the architect's thiscall color arithmetic and four-byte stack color values.
struct BdColor {
  // Pointer results preserve the observed receiver return; private pointer/reference/void spelling is unresolved.
  BdColor *Darken(i32 amount);
  BdColor *Scale(f32 factor);
  u32 value;
};

static_assert(sizeof(BdColor) == 4);

// Actual twenty-eight-byte directional-light record; the two intermediate float fields have no recovered use yet.
struct BdLight {
  f32 azimuth, elevation, field_008, field_00c, red, green, blue;
};

extern BdLight BdLights[24];
extern u32 BdConstructSelectedBlock;
extern u32 BdConstructRegBlock;
extern u32 BdConstructLeftPlane;
extern u32 BdConstructRightPlane;
extern u32 BdConstructFlatPlane;
extern u32 BdConstructLeftPlaneHandle;
extern u32 BdConstructRightPlaneHandle;
extern u32 BdConstructFlatPlaneHandle;
extern u32 BdConstructLeftPlaneHandleActive;
extern u32 BdConstructRightPlaneHandleActive;
extern u32 BdConstructFlatPlaneHandleActive;
extern u32 BdConstructFrameLineLeft;
extern u32 BdConstructFrameLineRight;
extern u32 BdConstructFrameLineBottom;
extern u32 BdConstructFaceOutline;
extern u32 BdConstructLeftGrid;
extern u32 BdConstructRightGrid;
extern u32 BdConstructBottomGrid;
extern u32 BdConstructIsoGrid;
extern u32 BdConstructLeftShadowOutline;
extern u32 BdConstructRightShadowOutline;
extern u32 BdConstructBottomShadowOutline;
extern u32 BdConstructShadow;
extern u32 BdConstructIsoVolumeEdge;
extern u32 BdBackgroundFill;
extern u32 BdSelectedHighlight;
extern u32 BdErasableHighlight;
extern u32 BdPropsBackground;
extern u32 BdPaletteBackground;
extern u32 BdBackgroundPixel;

// Private parser return declarations remain unresolved; candidates preserve the observed packed word/third pointer.
u32 BdParseColor(const ScString *text, u32 *out);
f32 *BdParseLight(const ScString *text, f32 *red, f32 *green, f32 *blue);
void BdLoadConfig();

bool BdFileExists(const ScStringView *name);
// Preserve observed byte results; the private source bool/byte spellings remain unresolved.
u8 BdProbeFile(const ScStringView *name, u8 truncate);
u8 BdCanWriteFile(const ScStringView *name);
i32 BdDeleteFile(const ScStringView *name);
bool BdMoveFile(const ScStringView *source, const ScStringView *destination);
bool BdCopyFile(const ScStringView *source, const ScStringView *destination, u8 overwrite);
bool BdHasFileAttributes(const ScStringView *name, u8 mask);
i32 BdSetFileAttributes(const ScStringView *name, u8 mask, u8 enabled);
u8 BdCreateTemporaryName(ScStringView *out);

class GxBuffer;
u8 BD_STDCALL BdDecodeRaw(GxBuffer *target, const u8 *source, u32 size, i32 format);
u8 BD_STDCALL BdDecodeRowRuns(GxBuffer *target, const u8 *source, u32 size, i32 format);
u8 BD_STDCALL BdDecodeColumnRuns(GxBuffer *target, const u8 *source, u32 size, i32 format);

class ScString;

u8 BdReadRegistryValue(const ScString *path, ScString *out);
u8 BdTransformRegistryBytes(const char *source, char *out, const char *key);

class ScFramework;
class GxGraphics;
class ScDirector;
class ScCom;
class RsFileSystem;
class RsManager;
class ScMessageServer;
class ScLanguageManager;
class ScCommandFactory;
class WnWindowManager;
class WnResources;

// Original director/framework accessors preserve the module's actual static owner and borrowed framework.
ScDirector *BdGetDirector();
ScCom *BdGetCom();
ScFramework *BdFrameWork();
GxGraphics *BdGetGraphics();
RsFileSystem *BdGetFileSystem();
RsManager *BdGetResourceManager();
ScMessageServer *BdGetMessageServer();
ScLanguageManager *BdGetLanguageManager();
ScCommandFactory *BdGetCommandFactory();
WnWindowManager *BdGetWindowManager();
WnResources *BdGetWindowResources();

class GxBuffer;

// Original export aggregate: five retained image slots followed by five corresponding rectangles.
struct BdThumbnailViews {
  GxBuffer *images[5];
  BdRect bounds[5];
};

bool BdTransparentColumn(GxBuffer *buffer, i32 x, i32 top, i32 height);
bool BdTransparentRow(GxBuffer *buffer, i32 y, i32 left, i32 width);
BdRect *BdFindImageBounds(GxBuffer *buffer, BdRect *out);
// The original incidental return varies by path; both installed callers use only the retained output.
void BdCropImage(GxBuffer *source, const BdRect *bounds, GxBuffer **out);
void BdConvertToRgb555(GxBuffer *source, GxBuffer **out);
void BdConvertToRgb565(GxBuffer *source, GxBuffer **out);
void BdShadeThumbnail(GxBuffer *buffer, f32 scale, u8 red, u8 green, u8 blue);
f64 BD_STDCALL BdScaleKernel(f32 value);
GxBuffer *BdScaleRgba(f32 scale, GxBuffer *source, const BdRect *bounds, GxBuffer *intermediate, GxBuffer *target);
// The paletted scaler accepts the original 555 and 565 source formats and optional work buffers.
GxBuffer *BdScalePalettedBicubic(f32 scale, GxBuffer *source, const BdRect *bounds, GxBuffer *intermediate,
    GxBuffer *target);

#ifndef BD_FASTCALL
#if defined(_WIN32)
#define BD_FASTCALL __fastcall
#else
#define BD_FASTCALL
#endif
#endif

class BdEditor;
class BdCatalog;
class BdToolPanel;
class BdToolSelection;
class BdToolGroupWindow;
class BdMiniMapCallback;
class BdGrid;
class BdRenderer;
class WnLabelControl;
class WnComboControl;
class GxBuffer;
class RsManager;

// Six source words describing a toolbar button, localized tip and image resource.
struct BdToolButtonSpec {
  BdToolButtonSpec();
  u32 identifier, text_group, text_instance, image_group, image_instance, flags;
};

u8 BD_STDCALL BdCreateToolButton(u32 mode, const BdToolButtonSpec *spec, WnButton **out);
u8 BD_STDCALL BdCreateToolStrip(WnWindow *parent, u32 identifier, const BdToolButtonSpec *buttons, i32 count);

// The menu group's primary interface precedes its independent root-window interface in the original object.
class BdToolMenuGroupView : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual u8 SetHeading(WnButton *button) = 0;
  virtual WnButton *GetHeading() = 0;
  virtual u8 AddButton(WnButton *button) = 0;
  virtual i32 GetButtonCount() = 0;
  virtual i32 GetVisible() = 0;
  virtual u32 SetVisible(u8 visible) = 0;
};

u8 BD_STDCALL BdAddMenuButton(BdToolMenuGroupView *group, u32 mode, const BdToolButtonSpec *spec);

// One toolbar menu group retains its heading and action buttons separately from its root-window children.
class BdToolMenuGroup : public BdToolMenuGroupView, public WnArchitectRootWidget {
public:
  BdToolMenuGroup();
  ~BdToolMenuGroup() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  u8 SetHeading(WnButton *button) override;
  WnButton *GetHeading() override;
  u8 AddButton(WnButton *button) override;
  i32 GetButtonCount() override;
  i32 GetVisible() override;
  u32 SetVisible(u8 visible) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;

private:
  WnButton *heading;
  WnButton **first, **last, **capacity;
};

// Original menu-container interface is distinct from its root-window view and shares its lifetime.
class BdToolMenuView : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual i32 GetGroupX() = 0;
  virtual i32 GetHeadingBottom() = 0;
  virtual void HideActive() = 0;
  virtual BdToolMenuGroupView *GetActive() = 0;
  virtual u8 AddGroup(BdToolMenuGroupView *group) = 0;
  virtual i32 SetIndent(i32 value) = 0;
  virtual i32 SetRowHeight(i32 value) = 0;
};

// Toolbar menu owns its groups and positions each heading alongside its popup window.
class BdToolMenu : public BdToolMenuView, public WnArchitectRootWidget {
public:
  BdToolMenu();
  ~BdToolMenu() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  i32 GetGroupX() override;
  i32 GetHeadingBottom() override;
  void HideActive() override;
  BdToolMenuGroupView *GetActive() override;
  u8 AddGroup(BdToolMenuGroupView *group) override;
  i32 SetIndent(i32 value) override;
  i32 SetRowHeight(i32 value) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  u8 Select(u32 identifier);

private:
  BdToolMenuGroupView *active;
  BdToolMenuGroupView **first, **last, **capacity;
  i32 group_x, heading_bottom, indent, row_height;
};

bool BdFitChildLabel(WnWindow *window, u32 identifier);
bool BdSetChildLabelFlag(WnWindow *window, u32 identifier, u32 mask, bool enabled);
u8 BD_STDCALL BdCreateToolIconImage(GxBuffer *source, const BdRect *bounds, u8 border, GxBuffer **out);
void BdScaleGroupColor(u8 *red, u8 *green, u8 *blue, f32 scale);

// A retained button and its original position in the tool item's content area.
struct BdToolIconEntry {
  WnButton *button;
  i32 x, y;
};

// Four-word linear interpolation state embedded in each toolbox item.
class BdInterpolator {
public:
  BdInterpolator(f32 initial, f32 unused);
  // Preserve the observed full register result; the private nominal return declarations remain unresolved.
  i64 Rebase(f32 start, u32 now);
  i64 Configure(f32 start, f32 target, f32 speed, u32 now);
  f64 Get(u32 now);

  f32 target, speed, velocity;
  u32 deadline;
};

// The tool item owns its icon entries and controls the scrollbar for its content area.
class BdToolItem : public WnArchitectRootWidget {
public:
  BdToolItem();
  ~BdToolItem() override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  virtual WnWindow *GetWindow();
  // The original private return declaration is unresolved; callers use only this operation's effects.
  virtual void AddIcon(WnButton *button, i32 x, i32 y);
  // These slots retain the observed effects and byte/word results; private source spellings remain unresolved.
  virtual void ClearIcons();
  virtual u8 SelectValue(i32 value);
  virtual i32 GetSelection();
  virtual u8 SelectValueVisible(i32 value);
  virtual void SetRows(i32 rows);
  virtual i32 SetContentHeight(i32 height);
  virtual i32 GetContentHeight();
  virtual i32 GetIconCount();
  virtual u8 SetBackground(u32 color);
  virtual u32 GetBackground();

protected:
  bool OnCommand(u32 kind, u32 command) override;

private:
  void LayoutIcons(i32 width, i32 height);
  void ClearSelection();
  u8 Select(i32 value, bool reveal);
  void StartScroll(i32 unused, u32 target);
  i32 selection, max_right, max_bottom;
  ScArray<BdToolIconEntry> icons;
  i32 content_height;
  u32 item_flags, background;
  i32 rows;
  u8 scrolling;
  f32 scroll_rate;
  i32 scroll_position;
  BdInterpolator scroll;
};

// This button adds parent-window hit testing to the standard image button.
class BdToolIcon : public WnImageButton {
protected:
  bool IsPointInWindow(i32 x, i32 y) const override;
};

u8 BD_STDCALL BdAddToolIcon(BdToolItem *item, u32 identifier, i32 x, i32 y, GxBuffer *image);

// Scoped image resource requested with the architect's image-factory options.
class BdImageResource {
public:
  BdImageResource(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~BdImageResource();
  bool Resolve(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

// The menu button's retained image and its original source and offset coordinates.
class BdButtonImage {
public:
  BdButtonImage(GxBuffer *buffer, const ScRect *rectangle, i32 x, i32 y);
  ~BdButtonImage();
  bool SetBuffer(GxBuffer *buffer);
  bool Transparent(i32 x, i32 y) const;

  GxBuffer *buffer;
  ScRect source;
  i32 offset_x, offset_y, origin_x, origin_y;
};

// Architect menu button with its own retained image and source-defined state flags.
class BdMenuButton : public WnArchitectRootWidget {
public:
  BdMenuButton();
  ~BdMenuButton() override;
  bool DoMessage(ScMessage *message) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool GZPaint() override;
  virtual WnWindow *GetWindow();
  virtual bool SetImage(GxBuffer *buffer);
  virtual bool Fit();
  virtual bool SetButtonFlag(u32 mask, bool enabled);

protected:
  bool GZOnCaptureChanged(WnWindow *window) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool IsPointInWindow(i32 x, i32 y) const override;

  BdButtonImage image;
  u32 button_flags;
};

// Resource and style fields used by the architect's label builder.
struct BdLabelSpec {
  u32 identifier, group, instance, alignment, font, foreground, background;
};

// Original nine-word menu-button descriptor, including its separately localized caption.
struct BdButtonSpec {
  u32 identifier, flags, image_group, image_instance, label_id, label_group, label_instance;
  i32 word_1c, label_y;
};

// Six original frame-clock words, including the capped elapsed interval and accumulated clock correction.
struct BdFrameTimer {
  u32 Update();
  u32 current, elapsed, sample, count, correction, limit;
};

extern BdFrameTimer BdWorkspaceTimer;

// The menu callback retains the architect director with each of its own references.
class BdMenuCallback : public UiWindowCallback {
public:
  BdMenuCallback();
  virtual ~BdMenuCallback();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool BuildWindow(WnWindow **window);
  u8 BuildMenu(WnWindow *window);
  u32 references;
};

// The toolbox callback owns its distinct retained panel and director-backed reference count.
class BdToolCallback : public UiWindowCallback {
public:
  BdToolCallback();
  virtual ~BdToolCallback();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool BuildWindow(WnWindow **window);
  u8 BuildPanel(WnWindow *window);
  u32 references;
  BdToolPanel *panel;
};

// New-building tile-size controls; the cancellation word remains separate from the window command IDs.
enum BdPreviewControlId : u32 {
  kBdPreviewCancelButtonId = 0xa3cca1c6,
  kBdPreviewTileOneId = 0x75c408c9,
  kBdPreviewTileTwoId = 0x75c408ca,
  kBdPreviewTileThreeId = 0x75c408cb,
  kBdPreviewTileFourId = 0x75c408cc,
  kBdPreviewTileFiveId = 0x75c408cd,
  kBdPreviewBackdropId = 0x45ee564e,
  kBdPreviewHeadingId = 0x15ee566f,
  kBdPreviewAcceptId = 0xa2fbb0b4,
  kBdPreviewTileOptionsId = 0xa2fbb0b6,
};

// Artwork used by the architect tool-item scrollbar.
inline constexpr u32 kBdToolItemScrollbarImageId = 0xe460e641;

// Result returned when the architect tile-size modal is accepted.
inline constexpr u32 kBdPreviewModalAccepted = 0x05c3f90c;

// Result returned when the architect tile-size modal is cancelled.
inline constexpr u32 kBdPreviewModalCancelled = 0x05c3f90d;

// The preview callback owns no state beyond its director-backed reference count.
class BdPreviewCallback : public UiWindowCallback {
public:
  BdPreviewCallback();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual u8 BuildWindow(WnWindow **window);
  virtual i32 ShowModal(u32 selection, u32 *result);
  u8 BuildScene(WnWindow *window);
  virtual ~BdPreviewCallback();
  u32 references;
};

// Resource-backed information window used by the workspace's modal information command.
class BdInfoCallback : public UiWindowCallback {
public:
  BdInfoCallback();
  virtual ~BdInfoCallback();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 BuildWindow(WnWindow **out);
  u8 BuildContent(WnWindow *window);
  // The private helpers' incidental return declarations are unresolved; their callers use only their effects.
  void CreateLabel(WnLabelControl **out, WnWindow *parent, i32 x, i32 y, i32 width, i32 height,
      const ScStringView *text, u32 alignment, u32 font);
  void CreateItem(WnWindow *parent, i32 *x, i32 *y, i32 width, u32 image, u32 heading, u32 detail);
  u32 references;
};

// Eight-word source descriptor for one architect tool group.
struct BdToolGroup {
  BdToolGroup();
  i32 SetColor(u8 red, u8 green, u8 blue);
  u32 word0, word1, word2, word3, word4, color, kind, word7;
};

// Secondary interface exposed by the architect's tool-group window under IID 35B808D8.
class BdToolGroupControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual u8 SetGroupFlags(u32 flags) = 0;
  virtual u8 SetGroupColor(u32 color) = 0;
  virtual u8 SetImageMode(u32 mode) = 0;
  virtual u8 SetGroupImage(GxBuffer *image) = 0;
  virtual u8 SetMargins(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual u8 SetTabGap(i32 gap) = 0;
};

// An item is borrowed; its primary window and the label are independently owned by the entry.
struct BdToolGroupEntry {
  BdToolGroupEntry(BdToolItem *item, const ScStringView &label);
  BdToolGroupEntry(const BdToolGroupEntry &source);
  ~BdToolGroupEntry();
  BdToolItem *item;
  WnWindow *window;
  ScString label;
  u32 field_01c;
  u8 flag;
};

// Tool-group window with its separate settings interface and retained tab control.
class BdToolGroupWindow : public WnArchitectRootWidget, public BdToolGroupControl {
public:
  BdToolGroupWindow();
  ~BdToolGroupWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  WnWindow *GetWindow() override;
  u8 SetGroupFlags(u32 flags) override;
  u8 SetGroupColor(u32 color) override;
  u8 SetImageMode(u32 mode) override;
  u8 SetGroupImage(GxBuffer *image) override;
  u8 SetMargins(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 SetTabGap(i32 gap) override;
  virtual bool AddItem(BdToolItem *item, const ScStringView &label);
  virtual u8 SelectIcon(u8 category, u32 value);
  virtual u8 HasItem(u32 identifier);
  virtual u8 GetItemFlag(u32 identifier);
  virtual void SetItemFlag(u32 identifier, u8 flag);
  virtual u8 GetItem(u32 identifier, BdToolItem **out);

private:
  void LayoutItems(i32 width, i32 height);
  void SelectTab(u32 identifier);
  u32 group_flags, group_color, dark_color, light_color, image_mode;
  GxBuffer *group_image;
  ScRect content_area;
  i32 margin_left, margin_top, margin_right, margin_bottom, tab_gap;
  WnComboControl *tab;
  ScArray<BdToolGroupEntry *> entries;
};

// The minimap window retains a grid and the scene state supplied by editor messages.
class BdMiniMapWindow : public WnArchitectRootWidget {
public:
  BdMiniMapWindow();
  ~BdMiniMapWindow() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  // The stock caller discards this operation's return; its private return declaration remains unresolved.
  virtual void Invalidate(u32 mask);
  void BD_FASTCALL SetGrid(BdGrid *value);
  // Preserve the observed receiver-pointer result; the private nominal return declaration is unresolved.
  BdMiniMapWindow *BD_FASTCALL Project(const BdPoint3 *point, BdPoint *out);
  // Preserve the final grid slice operation's observed result; the private nominal return declaration is unresolved.
  const BdVolume *RenderScene();
  // The private nominal return is unresolved; preserve the observed lock/unlock byte.
  u8 RefreshSurface();
  // The private nominal return is unresolved; painting consumes only the drawing effects.
  void DrawGizmo();

  BdGrid *grid;
  BdRenderer *renderer;
  BdCatalog *catalog;
  GxBuffer *image, *overlay, *surface, *scene;
  BdPoint offset;
  i32 zoom;
  f32 scale;
  BdVolume bounds, selection_bounds;
  BdPoint3 selected;
  BdRect selection_rectangle;
  BdPoint selection_point;
  u8 selection_mode, view_mode;
  u32 invalid_flags, interaction_flags;
  f32 zoom_factor;
  BdPoint scene_center;
  BdRect canvas_rectangle;
  BdRect gizmo_bounds;
  BdRect viewport;
  BdRect scene_rectangle;
  BdPoint drag_origin, drag_offset;
  BdPoint viewport_anchor;
  i32 view_offset;
  u32 colors[3];

protected:
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  bool GZOnMouseExit(WnWindow *window) override;
};

// The minimap callback has independent window-callback and message-target interfaces.
class BdMiniMapCallback : public UiWindowCallback, public ScMessageTarget {
public:
  BdMiniMapCallback();
  virtual ~BdMiniMapCallback();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool DoMessage(ScMessage *message) override;
  u8 BuildWindow(WnWindow **out);
  u8 BuildContent(WnWindow *window);
  u8 Close(WnWindow *window);
  u8 AddButton(WnWindow *parent, u32 identifier, const ScStringView *caption, i32 x, i32 y, GxBuffer *image,
      i32 style, u32 tip);

  u32 references;
  BdMiniMapWindow *map_window;
  u8 initialized;
};

u8 BD_STDCALL BdMiniMapGetDimensions(WnWindow *window, i32 *width, i32 *height);
u8 BD_STDCALL BdMiniMapGetLimits(WnWindow *window, ScRect *limits);

// The toolbox panel has independent generic-window and message-target interface views.
class BdToolPanel : public UiWindowCallback, public ScMessageTarget {
public:
  BdToolPanel();
  virtual ~BdToolPanel();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool DoMessage(ScMessage *message) override;
  bool BuildWindow(WnWindow **window);
  u8 BuildContent(WnWindow *window);
  u8 Dispatch(WnWindow *window, u32 command);
  // The caller discards the stock routine's incidental register result; its private return declaration is unresolved.
  void BuildMenu(WnWindow *window, BdToolMenuView **out);
  u8 BuildGroupWindow(WnWindow *window, const BdToolGroup *group, const ScRect *area);
  void BuildGroupWindows(WnWindow *window, const ScRect *area);
  u8 Close(WnWindow *window);
  void SelectGroup(i32 index);
  u8 SetGroupCommand(i32 index, u32 command);
  u32 GetGroupCommand(i32 index);
  u8 HideActiveWindow(WnWindow *window);
  u8 SetActiveWindow(WnWindow *window, u32 identifier);
  void ShowGroupButtons(WnWindow *window, i32 index);

  u32 references;
  BdToolGroup groups[6];
  u32 active_window_id;
  WnWindow *window;
  BdMiniMapCallback *field_0d4;
  BdToolSelection *selection;
};

// The tool-selection object has separate callback and message-target views; its remaining methods are unresolved.
class BdToolSelection : public UiWindowCallback, public ScMessageTarget {
public:
  BdToolSelection();
  virtual ~BdToolSelection();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool DoMessage(ScMessage *message) override;
  u8 Apply(u32 kind, u32 value);
  bool BuildAssetIcons(BdToolItem *item, BdCatalog *catalog, u32 kind);
  u8 BuildCompositionIcons(BdToolItem *item, BdCatalog *catalog, u32 group);
  u8 BuildShapeIcons(BdToolItem *item);
  u8 BuildIcons(WnWindow *window);
  u8 BuildWindow(WnWindow **out, u32 group);
  u8 Close(WnWindow *window);

  u32 references, group_id;
  BdToolGroupWindow **first, **last, **capacity;
};

u8 BD_STDCALL BdCanCloseWindow(WnWindow *window);
u8 BD_STDCALL BdSetChildVisible(WnWindow *window, u32 identifier, u8 visible);
u32 BD_STDCALL BdGetToolGroupID(WnWindow *window);
u8 BD_STDCALL BdGetToolGroupControl(WnWindow *window, BdToolGroupControl **out);
u8 BD_STDCALL BdSetToolGroupFlags(WnWindow *window, u16 flags);
u8 BD_STDCALL BdSetToolGroupColor(WnWindow *window, u32 color);
u8 BD_STDCALL BdSetToolGroupImageMode(WnWindow *window, i32 mode);
u8 BD_STDCALL BdSetToolGroupImage(WnWindow *window, GxBuffer *image);
u8 BD_STDCALL BdSetToolGroupMargins(WnWindow *window, i32 left, i32 top, i32 right, i32 bottom);
u8 BD_STDCALL BdCenterWindow(WnWindow *window);
u32 BdTextColor(WnLabelControl *control, u32 color);
u8 BD_STDCALL BdMakeLabel(WnWindow *parent, const BdLabelSpec *spec, i32 x, i32 y);
u8 BD_STDCALL BdMakeMenuButton(WnWindow *parent, const BdButtonSpec *spec, i32 x, i32 y);
u32 BdPreviewGetSelection(WnWindow *window, u32 identifier);
u8 BD_STDCALL BdPreviewEndModal(WnWindow *window, u32 result);
u8 BD_STDCALL BdPreviewCommand(WnWindow *window, u32 command);
u8 BD_STDCALL BdPreviewGetLimits(WnWindow *window, ScRect *limits);
u8 BD_STDCALL BdPreviewLayout(WnWindow *window, const ScRect *area);
u8 BD_STDCALL BdPreviewAddLabel(
    WnWindow *window, i32 x, i32 y, i32 width, i32 height, const ScStringView *text, WnLabelControl **out);
u8 BD_STDCALL BdPreviewAddButton(WnWindow *window, u32 identifier, i32 x, i32 y, const ScStringView *text);

// The architect workspace is a concrete root window. Its editor and child interfaces retain their own lifetimes.
// Message dispatch remains an explicit dependency until recovered.
class BdWorkspace : public WnArchitectRootWidget {
public:
  BdWorkspace();
  ~BdWorkspace() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool RegisterNotifications();
  bool UnregisterNotifications();
  u8 ConfirmDiscard();

  BdEditor *editor;
  BdMenuCallback *menu_callback;
  ScUnknown *field_0ac;
  BdPreviewCallback *preview_callback;
  ScUnknown *field_0b4;
  BdToolCallback *tool_callback;
  WnWindow *menu_window;
  WnWindow *field_0c0;
  WnWindow *tool_window;
  u8 field_0c8;
  u32 field_0cc;
  ScArray<u32> notifications;
};

u8 BdCreateDefaultGrid(i32 size);

#endif
