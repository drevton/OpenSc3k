#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include "buildings/architect/files.h"

#include <ctype.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <share.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "buildings/architect/api.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/manager.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/utf8.h"
#include "common/value.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// The original dialog creates two distinct overlapping preview outlines with identical bounds.
enum BdFilePreviewOutlineId : u32 {
  kBdFilePreviewFirstOutlineId = 0x2dfdd7c,
  kBdFilePreviewSecondOutlineId = 0x2dfdd7d,
};

// Installed label text identifies the three metadata rows; preview visibility follows the save/open toggle.
enum BdFileMetadataControl : u32 {
  kBdFileArchitectLabel = 0x2dfdd70,
  kBdFileBuildingTypeLabel = 0x2dfdd73,
  kBdFileDescriptionLabel = 0x2dfdd72,
  kBdFilePreviewToggle = 0x2dfdd7a,
  kBdFileSavePreviewPane = 0x2dfdd7b,
};

// Lighting colors retain the rounded Q31 reciprocal of 255 before the original floating-point scale.
enum BdLightChannelScale : i32 {
  kBdLightChannelNormalizationScale = 0x808081,
};

// Per-load color defaults remain independent of the live values subsequently read from BATConfig.INI.
enum BdConfigDefaultColor : u32 {
  kBdDefaultSelectedHighlight = 0xffffffff,
  kBdDefaultConstructLeftPlane = 0x80d5ebfd,
  kBdDefaultConstructRightPlane = 0x80d5ebfd,
  kBdDefaultConstructFlatPlane = 0x80d5ebfd,
  kBdDefaultConstructLeftPlaneHandle = 0x80d5ebfd,
  kBdDefaultConstructRightPlaneHandle = 0x80d5ebfd,
  kBdDefaultConstructFlatPlaneHandle = 0x80d5ebfd,
  kBdDefaultConstructLeftPlaneHandleActive = 0x80d5ebfd,
  kBdDefaultConstructRightPlaneHandleActive = 0x80d5ebfd,
  kBdDefaultConstructFlatPlaneHandleActive = 0x80d5ebfd,
  kBdDefaultConstructSelectedBlock = 0x80ebeba5,
  kBdDefaultConstructRegBlock = 0x80a5aeeb,
  kBdDefaultConstructFrameLineLeft = 0xff6257aa,
  kBdDefaultConstructFrameLineRight = 0xffa87276,
  kBdDefaultConstructFrameLineBottom = 0xff3ca03f,
  kBdDefaultConstructFaceOutline = 0xffebeba5,
  kBdDefaultConstructLeftGrid = 0xffbfbfbf,
  kBdDefaultConstructRightGrid = 0xffbfbfbf,
  kBdDefaultConstructBottomGrid = 0xffbfbfbf,
  kBdDefaultConstructIsoGrid = 0xffbfbfbf,
  kBdDefaultConstructLeftShadowOutline = 0xffae322a,
  kBdDefaultConstructRightShadowOutline = 0xff3a32a2,
  kBdDefaultConstructBottomShadowOutline = 0xff007400,
  kBdDefaultConstructShadow = 0xff969696,
  kBdDefaultConstructIsoVolumeEdge = 0xff878787,
  kBdDefaultBackgroundFill = 0xffded6e8,
  kBdDefaultErasableHighlight = 0xffff0f0f,
  kBdDefaultPropsBackground = 0xff040408,
};

enum BdFileDialogExtraControlId : u32 {
  kBdFileDialogWindow = 0x02dfdd67,
  kBdFileBuildingNameEdit = 0x02dfdd74,
  kBdFileDescriptionEdit = 0x02dfdd76,
  kBdFileBuildingTypeControl = 0x02dfdd77,
  kBdFilePreviewBitmap = 0x02dfdd7e,
  kBdFileSizeCaption = 0x02dfdd7f,
  kBdFileSizeValue = 0x02dfdd80,
};

// Building-file controls have their own roles even where their numeric IDs match the city picker.
enum BdFileDialogControl : u32 {
  kBdFileCancelButton = 0x02dfdd68,
  kBdFileAcceptButton = 0x02dfdd6a,
  kBdFileDirectoryCombo = 0x02dfdd6c,
  kBdFileNameEdit = 0x02dfdd6e,
  kBdFileList = 0x02dfdd6f,
  kBdFileDirectoryLabel = 0x02dfdd78,
  kBdFileNameLabel = 0x02dfdd79,
};

#if defined(_WIN32)
static constexpr char BdNativeSeparator = '\\';
static constexpr const char *BdNativeSeparatorString = "\\";
#else
static constexpr char BdNativeSeparator = '/';
static constexpr const char *BdNativeSeparatorString = "/";
#endif

// Decode complete decimal channel lists and report malformed settings before changing their destinations.
static bool BdReadChannels(const ScString *text, i32 *out, u32 count) {
  const char *cursor = text->Data();
  for (u32 i = 0; i < count; ++i) {
    char *end;
    errno = 0;
    long value = strtol(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || value < INT_MIN || value > INT_MAX)
      return false;
    out[i] = static_cast<i32>(value);
    if (i + 1 != count) {
      if (*end != ',')
        return false;
      cursor = end + 1;
    }
  }
  return true;
}

// Pack four original decimal components without masking their upper bits; unsigned shifts preserve the machine result.
u32 BdParseColor(const ScString *text, u32 *out) {
  i32 channels[4];
  if (!BdReadChannels(text, channels, 4))
    return 0;
  u32 value = static_cast<u32>(channels[0]) << 24 | static_cast<u32>(channels[1]) << 16 |
      static_cast<u32>(channels[2]) << 8 | static_cast<u32>(channels[3]);
  *out = value;
  return value;
}

// Scale three signed decimal components by the source float constant; the observed third-output pointer is returned.
f32 *BdParseLight(const ScString *text, f32 *red, f32 *green, f32 *blue) {
  i32 channels[3];
  if (!BdReadChannels(text, channels, 3))
    return blue;
  *red = static_cast<f32>(static_cast<i64>(channels[0]) * kBdLightChannelNormalizationScale) * 0x1p-31f;
  *green = static_cast<f32>(static_cast<i64>(channels[1]) * kBdLightChannelNormalizationScale) * 0x1p-31f;
  *blue = static_cast<f32>(static_cast<i64>(channels[2]) * kBdLightChannelNormalizationScale) * 0x1p-31f;
  return blue;
}

// Read one actual color setting and preserve its existing global when the key is absent.
static void BdReadColorSetting(BdIniFile *reader, const char *key, ScString *value, u32 *out) {
  ScString section("Colors", 6), name(key, static_cast<u32>(strlen(key)));
  if (reader->ReadEntry(section, name, value))
    BdParseColor(value, out);
}

// Read one directional light setting, copying the top light to its original second direction when requested.
static void BdReadLightSetting(BdIniFile *reader, const char *key, ScString *value, u32 index, bool paired) {
  ScString section("Lights", 6), name(key, static_cast<u32>(strlen(key)));
  if (reader->ReadEntry(section, name, value)) {
    i32 channels[3];
    if (!BdReadChannels(value, channels, 3))
      return;
    BdLight *light = BdLights + index;
    BdParseLight(value, &light->red, &light->green, &light->blue);
    if (paired) {
      light[1].red = light->red;
      light[1].green = light->green;
      light[1].blue = light->blue;
    }
  }
}

// Preserve the actual CRT initializer/ as constant aggregate initialization.
// It only writes these light records; configuration later replaces their RGB fields while retaining directions.
BdLight BdLights[24] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.5647059082984924f, 0.5647059082984924f, 0.5647059082984924f},
    {1.5707963705062866f, 0.0f, 0.0f, 0.0f, 0.3137255012989044f, 0.3137255012989044f, 0.3137255012989044f},
    {0.0f, 3.1415927410125732f, 0.0f, 0.0f, 0.5647059082984924f, 0.5647059082984924f, 0.5647059082984924f},
    {1.5707963705062866f, 3.1415927410125732f, 0.0f, 0.0f, 0.3137255012989044f, 0.3137255012989044f,
        0.3137255012989044f},
    {0.2617993950843811f, 1.5707963705062866f, 0.0f, 0.0f, 0.45098039507865906f, 0.45098039507865906f,
        0.45098039507865906f},
    {0.2617993950843811f, 4.71238899230957f, 0.0f, 0.0f, 0.45098039507865906f, 0.45098039507865906f,
        0.45098039507865906f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.5647059082984924f, 0.5647059082984924f, 0.5647059082984924f},
    {1.5707963705062866f, 0.0f, 0.0f, 0.0f, 0.3137255012989044f, 0.3137255012989044f, 0.3137255012989044f},
    {0.0f, 3.1415927410125732f, 0.0f, 0.0f, 0.5647059082984924f, 0.5647059082984924f, 0.5647059082984924f},
    {1.5707963705062866f, 3.1415927410125732f, 0.0f, 0.0f, 0.3137255012989044f, 0.3137255012989044f,
        0.3137255012989044f},
    {0.2617993950843811f, 1.5707963705062866f, 0.0f, 0.0f, 0.45098039507865906f, 0.45098039507865906f,
        0.45098039507865906f},
    {0.2617993950843811f, 4.71238899230957f, 0.0f, 0.0f, 0.45098039507865906f, 0.45098039507865906f,
        0.45098039507865906f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.9764705896377563f, 0.9098039269447327f},
    {1.5707963705062866f, 0.0f, 0.0f, 0.0f, 0.3843137323856354f, 0.3843137323856354f, 0.5215686559677124f},
    {0.0f, 3.1415927410125732f, 0.0f, 0.0f, 0.3843137323856354f, 0.3843137323856354f, 0.5215686559677124f},
    {1.5707963705062866f, 3.1415927410125732f, 0.0f, 0.0f, 1.0f, 0.9764705896377563f, 0.9098039269447327f},
    {0.2617993950843811f, 1.5707963705062866f, 0.0f, 0.0f, 0.49803921580314636f, 0.49803921580314636f,
        0.49803921580314636f},
    {0.2617993950843811f, 4.71238899230957f, 0.0f, 0.0f, 0.49803921580314636f, 0.49803921580314636f,
        0.49803921580314636f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};

u32 BdConstructSelectedBlock;
u32 BdConstructRegBlock;
u32 BdConstructLeftPlane;
u32 BdConstructRightPlane;
u32 BdConstructFlatPlane;
u32 BdConstructLeftPlaneHandle;
u32 BdConstructRightPlaneHandle;
u32 BdConstructFlatPlaneHandle;
u32 BdConstructLeftPlaneHandleActive;
u32 BdConstructRightPlaneHandleActive;
u32 BdConstructFlatPlaneHandleActive;
u32 BdConstructFrameLineLeft;
u32 BdConstructFrameLineRight;
u32 BdConstructFrameLineBottom;
u32 BdConstructFaceOutline;
u32 BdConstructLeftGrid;
u32 BdConstructRightGrid;
u32 BdConstructBottomGrid;
u32 BdConstructIsoGrid;
u32 BdConstructLeftShadowOutline;
u32 BdConstructRightShadowOutline;
u32 BdConstructBottomShadowOutline;
u32 BdConstructShadow;
u32 BdConstructIsoVolumeEdge;
u32 BdBackgroundFill;
u32 BdSelectedHighlight;
u32 BdErasableHighlight;
u32 BdPropsBackground;
u32 BdPaletteBackground;
u32 BdBackgroundPixel;

void BdLoadConfig() {
  BdSelectedHighlight = kBdDefaultSelectedHighlight;
  BdLights[0].red = 0.56470591f;
  BdLights[0].green = 0.56470591f;
  BdLights[0].blue = 0.56470591f;
  BdConstructLeftPlane = kBdDefaultConstructLeftPlane;
  BdConstructRightPlane = kBdDefaultConstructRightPlane;
  BdLights[1].red = 0.3137255f;
  BdLights[1].green = 0.3137255f;
  BdLights[1].blue = 0.3137255f;
  BdConstructFlatPlane = kBdDefaultConstructFlatPlane;
  BdConstructLeftPlaneHandle = kBdDefaultConstructLeftPlaneHandle;
  BdLights[2].red = 0.56470591f;
  BdLights[2].green = 0.56470591f;
  BdLights[2].blue = 0.56470591f;
  BdConstructRightPlaneHandle = kBdDefaultConstructRightPlaneHandle;
  BdConstructFlatPlaneHandle = kBdDefaultConstructFlatPlaneHandle;
  BdLights[3].red = 0.3137255f;
  BdLights[3].green = 0.3137255f;
  BdLights[3].blue = 0.3137255f;
  BdConstructLeftPlaneHandleActive = kBdDefaultConstructLeftPlaneHandleActive;
  BdConstructRightPlaneHandleActive = kBdDefaultConstructRightPlaneHandleActive;
  BdLights[4].red = 0.49803922f;
  BdLights[4].green = 0.49803922f;
  BdLights[4].blue = 0.49803922f;
  BdLights[5].red = 0.49803922f;
  BdLights[5].green = 0.49803922f;
  BdLights[5].blue = 0.49803922f;
  BdConstructFlatPlaneHandleActive = kBdDefaultConstructFlatPlaneHandleActive;
  BdLights[8].red = 1.0f;
  BdLights[8].green = 0.94117647f;
  BdConstructSelectedBlock = kBdDefaultConstructSelectedBlock;
  BdLights[8].blue = 0.84313726f;
  BdConstructRegBlock = kBdDefaultConstructRegBlock;
  BdConstructFrameLineLeft = kBdDefaultConstructFrameLineLeft;
  BdLights[9].red = 0.87843138f;
  BdConstructFrameLineRight = kBdDefaultConstructFrameLineRight;
  BdConstructFrameLineBottom = kBdDefaultConstructFrameLineBottom;
  BdLights[9].green = 0.96470588f;
  BdConstructFaceOutline = kBdDefaultConstructFaceOutline;
  BdConstructLeftGrid = kBdDefaultConstructLeftGrid;
  BdLights[9].blue = 1.0f;
  BdConstructRightGrid = kBdDefaultConstructRightGrid;
  BdConstructBottomGrid = kBdDefaultConstructBottomGrid;
  BdLights[10].red = 0.87843138f;
  BdConstructIsoGrid = kBdDefaultConstructIsoGrid;
  BdConstructLeftShadowOutline = kBdDefaultConstructLeftShadowOutline;
  BdLights[10].green = 0.96470588f;
  BdConstructRightShadowOutline = kBdDefaultConstructRightShadowOutline;
  BdConstructBottomShadowOutline = kBdDefaultConstructBottomShadowOutline;
  BdLights[10].blue = 1.0f;
  BdConstructShadow = kBdDefaultConstructShadow;
  BdConstructIsoVolumeEdge = kBdDefaultConstructIsoVolumeEdge;
  BdLights[11].red = 1.0f;
  BdBackgroundFill = kBdDefaultBackgroundFill;
  BdErasableHighlight = kBdDefaultErasableHighlight;
  BdLights[11].green = 0.94117647f;
  BdPropsBackground = kBdDefaultPropsBackground;
  BdLights[11].blue = 0.84313726f;
  BdLights[12].red = 0.49803922f;
  BdLights[12].green = 0.49803922f;
  BdLights[18].green = 0.9647058844566345f;
  BdLights[12].blue = 0.49803922f;
  BdLights[19].blue = 0.843137264251709f;
  BdLights[13].red = 0.49803922f;
  BdLights[13].green = 0.49803922f;
  BdLights[13].blue = 0.49803922f;
  BdLights[16].red = 1.0f;
  BdLights[16].green = 0.94117647f;
  BdLights[16].blue = 0.84313726f;
  BdLights[17].red = 0.87843138f;
  BdLights[17].green = 0.96470588f;
  BdLights[17].blue = 1.0f;
  BdLights[18].red = 0.87843138f;
  BdLights[18].blue = 1.0f;
  BdLights[19].red = 1.0f;
  BdLights[19].green = 0.94117647f;
  BdLights[20].red = 0.49803922f;
  BdLights[20].green = 0.49803922f;
  BdLights[20].blue = 0.49803922f;
  BdLights[21].red = 0.49803922f;
  BdLights[21].green = 0.49803922f;
  BdLights[21].blue = 0.49803922f;
  ScString path, value;
  BdIniFile reader;
  const char *directory = BdGetFileSystem()->DataDirectory();
  path.FromChar(directory);
  path.Append("Res\\BA\\BATConfig.INI");
  reader.SetPath(path);
  BdReadColorSetting(&reader, "ConstructSelectedBlock", &value, &BdConstructSelectedBlock);
  BdReadColorSetting(&reader, "ConstructRegBlock", &value, &BdConstructRegBlock);
  BdReadColorSetting(&reader, "ConstructLeftPlane", &value, &BdConstructLeftPlane);
  BdReadColorSetting(&reader, "ConstructRightPlane", &value, &BdConstructRightPlane);
  BdReadColorSetting(&reader, "ConstructFlatPlane", &value, &BdConstructFlatPlane);
  BdReadColorSetting(&reader, "ConstructLeftPlaneHandle", &value, &BdConstructLeftPlaneHandle);
  BdReadColorSetting(&reader, "ConstructRightPlaneHandle", &value, &BdConstructRightPlaneHandle);
  BdReadColorSetting(&reader, "ConstructFlatPlaneHandle", &value, &BdConstructFlatPlaneHandle);
  BdReadColorSetting(&reader, "ConstructLeftPlaneHandleActive", &value, &BdConstructLeftPlaneHandleActive);
  BdReadColorSetting(&reader, "ConstructRightPlaneHandleActive", &value, &BdConstructRightPlaneHandleActive);
  BdReadColorSetting(&reader, "ConstructFlatPlaneHandleActive", &value, &BdConstructFlatPlaneHandleActive);
  BdReadColorSetting(&reader, "ConstructFrameLineLeft", &value, &BdConstructFrameLineLeft);
  BdReadColorSetting(&reader, "ConstructFrameLineRight", &value, &BdConstructFrameLineRight);
  BdReadColorSetting(&reader, "ConstructFrameLineBottom", &value, &BdConstructFrameLineBottom);
  BdReadColorSetting(&reader, "ConstructFaceOutline", &value, &BdConstructFaceOutline);
  BdReadColorSetting(&reader, "ConstructLeftGrid", &value, &BdConstructLeftGrid);
  BdReadColorSetting(&reader, "ConstructRightGrid", &value, &BdConstructRightGrid);
  BdReadColorSetting(&reader, "ConstructBottomGrid", &value, &BdConstructBottomGrid);
  BdReadColorSetting(&reader, "ConstructIsoGrid", &value, &BdConstructIsoGrid);
  BdReadColorSetting(&reader, "ConstructLeftShadowOutline", &value, &BdConstructLeftShadowOutline);
  BdReadColorSetting(&reader, "ConstructRightShadowOutline", &value, &BdConstructRightShadowOutline);
  BdReadColorSetting(&reader, "ConstructBottomShadowOutline", &value, &BdConstructBottomShadowOutline);
  BdReadColorSetting(&reader, "ConstructShadow", &value, &BdConstructShadow);
  BdReadColorSetting(&reader, "ConstructIsoVolumeEdge", &value, &BdConstructIsoVolumeEdge);
  BdReadColorSetting(&reader, "BackgroundFill", &value, &BdBackgroundFill);
  BdReadColorSetting(&reader, "SelectedHighlight", &value, &BdSelectedHighlight);
  BdReadColorSetting(&reader, "ErasableHighlight", &value, &BdErasableHighlight);
  BdReadColorSetting(&reader, "PropsBackground", &value, &BdPropsBackground);
  BdReadColorSetting(&reader, "PaletteBackground", &value, &BdPaletteBackground);
  BdReadLightSetting(&reader, "ConstructNorth", &value, 0, false);
  BdReadLightSetting(&reader, "ConstructWest", &value, 1, false);
  BdReadLightSetting(&reader, "ConstructSouth", &value, 2, false);
  BdReadLightSetting(&reader, "ConstructEast", &value, 3, false);
  BdReadLightSetting(&reader, "ConstructTop", &value, 4, true);
  BdReadLightSetting(&reader, "TextureNorth", &value, 8, false);
  BdReadLightSetting(&reader, "TextureWest", &value, 9, false);
  BdReadLightSetting(&reader, "TextureSouth", &value, 10, false);
  BdReadLightSetting(&reader, "TextureEast", &value, 11, false);
  BdReadLightSetting(&reader, "TextureTop", &value, 12, true);
  BdReadLightSetting(&reader, "PreviewNorth", &value, 16, false);
  BdReadLightSetting(&reader, "PreviewWest", &value, 17, false);
  BdReadLightSetting(&reader, "PreviewSouth", &value, 18, false);
  BdReadLightSetting(&reader, "PreviewEast", &value, 19, false);
  BdReadLightSetting(&reader, "PreviewTop", &value, 20, true);
  i32 code = static_cast<i32>(BdGetGraphics()->GetScreenColorType().color);
  if (code != 5 && code != 7 && code != 10)
    return;
  u32 color = BdBackgroundFill;
  u8 red = static_cast<u8>(color >> 16), green = static_cast<u8>(color >> 8), blue = static_cast<u8>(color);
  BdBackgroundPixel =
      code == 7 ? (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3 : (red >> 3) << 10 | (green >> 3) << 5 | blue >> 3;
}

BdDirectoryEntries::BdDirectoryEntries() : head(new BdDirectoryNode) {
  head->next = head;
  head->previous = head;
}

BdDirectoryEntries::~BdDirectoryEntries() {
  Clear();
  delete head;
}

// Remove each owned result in forward list order and reset the circular links.
void BdDirectoryEntries::Clear() {
  BdDirectoryNode *node = head->next;
  while (node != head) {
    BdDirectoryNode *next = node->next;
    delete node;
    node = next;
  }
  head->next = head;
  head->previous = head;
}

// Insert one result before the specified list position, preserving the source enumeration order.
void BdDirectoryEntries::Insert(BdDirectoryNode *before, BdDirectorySearch *owner, const char *name, u32 kind) {
  BdDirectoryNode *node = new BdDirectoryNode;
  node->entry.owner = owner;
  node->entry.name.FromChar(name);
  node->entry.kind = kind;
  node->previous = before->previous;
  node->next = before;
  before->previous->next = node;
  before->previous = node;
}

bool BdDirectoryEntries::Contains(const char *name) const {
  for (BdDirectoryNode *node = head->next; node != head; node = node->next)
    if (!strcmp(node->entry.name.Data(), name))
      return true;
  return false;
}

BdDirectoryNames::~BdDirectoryNames() {
  Clear();
}

// Destroy the retained names in order and leave the listing ready for another enumeration.
void BdDirectoryNames::Clear() {
  for (u32 i = 0; i < names.size; ++i)
    delete names.data[i];
  names.Clear();
}

void BdDirectoryNames::Append(const char *name) {
  *names.Append() = new ScString(name);
}

// Insert one independent special-directory name at the beginning of the listing.
void BdDirectoryNames::Prepend(const char *name) {
  names.Append();
  for (u32 i = names.size - 1; i; --i)
    names.data[i] = names.data[i - 1];
  names.data[0] = new ScString(name);
}

// The listing owns the strings, so only their pointers move while the sorted sequence is assembled.
void BdDirectoryNames::Sort() {
  for (u32 i = 1; i < names.size; ++i) {
    ScString *value = names.data[i];
    u32 j = i;
    while (j) {
      ScString *previous = names.data[j - 1];
      u32 value_length = value->Strlen(), previous_length = previous->Strlen();
      u32 length = value_length < previous_length ? value_length : previous_length;
      int order = memcmp(value->Data(), previous->Data(), length);
      if (order > 0 || (!order && value_length >= previous_length))
        break;
      names.data[j] = previous;
      --j;
    }
    names.data[j] = value;
  }
}

// The original lowercases each root's first character before querying the drive type.
u8 BdGetLogicalDrives(BdDirectoryNames *names, ScArray<u32> *kinds) {
  names->Clear();
  kinds->Clear();
#if !defined(_WIN32)
  names->Append("/");
  *kinds->Append() = 0;
  return 1;
#else
  DWORD length = GetLogicalDriveStringsA(0, nullptr);
  if (!length)
    return 0;
  char *buffer = new char[length + 2];
  u8 result = 0;
  if (GetLogicalDriveStringsA(length + 1, buffer)) {
    DWORD offset = 0, limit = length - 1;
    while (offset < limit) {
      char *drive = buffer + offset;
      if (*drive < 'a')
        *drive += 32;
      names->Append(drive);
      u32 kind;
      switch (GetDriveTypeA(drive)) {
      case DRIVE_FIXED:
        kind = 1;
        break;
      case DRIVE_REMOVABLE:
        kind = 2;
        break;
      case DRIVE_CDROM:
        kind = 3;
        break;
      case DRIVE_RAMDISK:
        kind = 4;
        break;
      case DRIVE_REMOTE:
        kind = 5;
        break;
      default:
        kind = 0;
        break;
      }
      *kinds->Append() = kind;
      while (offset < limit && buffer[offset])
        ++offset;
      ++offset;
    }
    result = 1;
  }
  delete[] buffer;
  return result;
#endif
}

// Copy the search directory, select the original wildcard and optionally scan immediately.
// A nonempty directory gains a trailing backslash unless it already ends in a slash.
BdDirectorySearch::BdDirectorySearch(const ScString *path, u8 scan, u8 parent, u8 current)
    : directory(*path), pattern("*.*"), include_parent(parent), include_current(current) {
  u32 length = directory.Strlen();
  if (length && directory.Data()[length - 1] != '\\' && directory.Data()[length - 1] != '/')
    directory.Append(BdNativeSeparatorString);
  if (scan)
    Scan();
}

// Retire the original search's wildcard, directory and owned result list in member destruction order.
BdDirectorySearch::~BdDirectorySearch() = default;

// Enumerate matching Win32 entries into the owned result list in API order, classifying directories and files.
// Include or synthesize the current and parent entries only when their respective original flags request them.
u8 BdDirectorySearch::Scan() {
  entries.Clear();
  ScDirectory search(directory, false, false, false);
  search.SetNewDirectoryEntryFilter(pattern, false);
  ScDirectoryNames found;
  if (!search.ReadNames(&found, 12))
    return 0;
  for (u32 i = 0; i < found.entries.size; ++i) {
    ScString full(directory);
    full.Append(found.entries.data[i]->Data());
    ScString native = ScNativeFilePath(full.Data());
    SDL_PathInfo info;
    if (SDL_GetPathInfo(native.Data(), &info))
      entries.Insert(entries.head, this, found.entries.data[i]->Data(), info.type == SDL_PATHTYPE_DIRECTORY ? 4 : 8);
  }
  bool all = pattern.Strlen() == 3 && !memcmp(pattern.Data(), "*.*", 3);
  if (include_parent && all && !entries.Contains(".."))
    entries.Insert(entries.head->next, this, "..", 4);
  if (include_current && all && !entries.Contains("."))
    entries.Insert(entries.head->next, this, ".", 4);
  return 1;
}

// When requested, immediately replace the search's retained results through the original scan.
void BdDirectorySearch::SetPattern(const ScString *value, u8 scan) {
  pattern = *value;
  if (!pattern.Strlen()) {
    ScString fallback("*.*");
    pattern = fallback;
  }
  if (scan)
    Scan();
}

// Append names matching the current directory and wildcard, selecting files and directories with the original bits.
// Missing dot entries are prepended only after a successful scan with the default wildcard.
u8 BdDirectorySearch::GetNames(BdDirectoryNames *out, u8 flags) {
  ScDirectory search(directory, false, false, false);
  search.SetNewDirectoryEntryFilter(pattern, false);
  ScDirectoryNames found;
  if (!search.ReadNames(&found, flags & 12))
    return 0;
  for (u32 i = 0; i < found.entries.size; ++i)
    out->Append(found.entries.data[i]->Data());
  bool all = pattern.Strlen() == 3 && !memcmp(pattern.Data(), "*.*", 3);
  if ((flags & 2) && all)
    out->Prepend("..");
  if ((flags & 1) && all)
    out->Prepend(".");
  return 1;
}


// Test the string interface's null-terminated path with the installed Windows attribute query.
bool BdFileExists(const ScStringView *name) {
  return RsFile::FileExists(*name);
}

// Open or create the named file with the original read/share flags, optionally truncating its contents.
// A successful open is always closed before the success byte is returned.
u8 BdProbeFile(const ScStringView *name, u8 truncate) {
  RsFile file(*name);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenAlways, kRsFileShareReadWrite))
    return 0;
  if (truncate) {
    file.SeekBegin();
    file.Resize(0);
  }
  file.Close();
  return 1;
}

// Delete the named path through the original ANSI Windows file operation.
i32 BdDeleteFile(const ScStringView *name) {
  return RsFile::Delete(*name);
}

// Move an architect file with cross-volume copying when supported, falling back to the original basic move.
// A successful fallback disables the cached extended operation for subsequent calls.
bool BdMoveFile(const ScStringView *source, const ScStringView *destination) {
  return RsFile::Rename(*source, *destination);
}

// Copy an architect file through the Windows file API, allowing replacement only when requested.
bool BdCopyFile(const ScStringView *source, const ScStringView *destination, u8 overwrite) {
  return RsFile::Copy(*source, *destination, overwrite != 0);
}

// Check write access to an existing file through the resource file, or probe and remove a newly created path.
// The original ignores deletion failure after a successful creation probe.
u8 BdCanWriteFile(const ScStringView *name) {
  if (BdFileExists(name)) {
    RsFile file(*name);
    if (!file.Open(kRsFileAccessWrite, kRsFileOpenExisting, kRsFileShareRead))
      return 0;
    file.Close();
    return 1;
  }
  if (!BdProbeFile(name, 0))
    return 0;
  BdDeleteFile(name);
  return 1;
}

// Test the requested read-only and hidden attribute bits from the original byte-sized query result.
bool BdHasFileAttributes(const ScStringView *name, u8 mask) {
  u8 attributes = static_cast<u8>(ScGetNativeFileAttributes(name->ToChar()));
  return (!(mask & kScFileAttributeReadOnly) || (attributes & kScFileAttributeReadOnly)) &&
      (!(mask & kScFileAttributeHidden) || (attributes & kScFileAttributeHidden));
}

// Set or clear only the requested read-only and hidden bits while preserving the other attribute bits.
// Normalize the native result to the original full-width 0/1 BOOL and preserve the second live path query.
i32 BdSetFileAttributes(const ScStringView *name, u8 mask, u8 enabled) {
  u32 attributes = ScGetNativeFileAttributes(name->ToChar());
  if (mask & kScFileAttributeReadOnly)
    attributes = enabled ? attributes | kScFileAttributeReadOnly : attributes & ~kScFileAttributeReadOnly;
  if (mask & kScFileAttributeHidden)
    attributes = enabled ? attributes | kScFileAttributeHidden : attributes & ~kScFileAttributeHidden;
  return ScSetNativeFileAttributes(name->ToChar(), attributes) != 0;
}

// Reserve an RZ_ timestamp-named temporary file in the current directory, trying earlier timestamps on collisions.
// Publish the selected bytes only after the original exclusive create succeeds and the descriptor closes.
u8 BdCreateTemporaryName(ScStringView *out) {
  return RsFile::MakeTempPathName(out);
}

// Initialize the dialog's owned lists and collect the available localized type identifiers.
// Missing type resources are omitted in the original lookup order.
BdFileDialog::BdFileDialog()
    : references(0), mode(0), field_0d7(0), filters(nullptr), filter_count(0), filter_capacity(0),
      entries(new BdFileEntry) {
  entries->next = entries;
  entries->previous = entries;
  for (u32 identifier = 520; identifier <= 535; ++identifier) {
    BdTextResource resource(identifier, kRsArchitectTextGroup);
    if (resource.text)
      *available_types.Append() = identifier;
  }
}

// Destroy the circular file list and the owned filter strings.
// The remaining strings and available-type array release their own storage after the body.
BdFileDialog::~BdFileDialog() {
  BdFileEntry *entry = entries->next;
  while (entry != entries) {
    BdFileEntry *next = entry->next;
    delete entry;
    entry = next;
  }
  delete entries;
  delete[] filters;
}

// Expose the original unknown and window-callback identities and retain the selected interface.
bool BdFileDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 BdFileDialog::AddRef() {
  return references++;
}

// Drop a callback reference and destroy the dialog at its final release.
// A nonfinal release returns the count from before the decrement.
u32 BdFileDialog::Release() {
  u32 previous = references;
  if (previous > 1) {
    --references;
    return previous;
  }
  delete this;
  return 0;
}

// Create the dialog's generic window, configure its original styles and install this callback.
// The generic interface remains owned by the created window as in the original factory path.
bool BdFileDialog::BuildWindow(WnWindow **out) {
  if (!out)
    return false;
  UiGenericInterface *generic = nullptr;
  if (!BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericBackButton, 0);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->GetWindow()->PrivateBuffer(true);
  generic->SetCallback(this);
  generic->GetWindow()->SetID(kBdFileDialogWindow);
  *out = generic->GetWindow();
  return true;
}

// Dispatch generic-window messages to the original dialog operations and preserve their results.
bool BdFileDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    switch (first) {
    case 1:
      return Commit(window, static_cast<u32>(second));
    case 6:
      return SelectEntry(window);
    case 8:
      return EditEntry(window, static_cast<u32>(second));
    case 9:
      return ChangeEntry(window, static_cast<u32>(second));
    case 11:
      Populate(window);
      return 1;
    case 15:
      return ActivateEntry(window, static_cast<u32>(second));
    default:
      return 0;
    }
  }
  if (message == kWnMsg_KeyDown) {
    if (first != 13)
      return 0;
    if (field_00c.Strlen()) {
      WnWindow *child = window->GetChildWindowFromID(kBdFileAcceptButton);
      if (child && child->GetFlag(kWnFlagEnabled))
        Commit(window, kBdFileAcceptButton);
    }
    return 1;
  }
  if (message == kWnMsg_Init)
    return BuildContent(window, static_cast<u32>(first));
  if (message == kWnMsg_Shutdown)
    return Close(window);
  if (message == kWnMsg_ClientConstraints)
    return GetLimits(window, reinterpret_cast<ScRect *>(first));
  return 0;
}

u8 BdFileDialog::Close(WnWindow *) {
  mode = 0;
  return 1;
}

// Publish the original fixed width and height limits for the dialog window.
// A null output is rejected without modifying other state.
u8 BdFileDialog::GetLimits(WnWindow *, ScRect *limits) {
  if (!limits)
    return 0;
  limits->left = 470;
  limits->top = 330;
  limits->right = 470;
  limits->bottom = 421;
  return 1;
}

// Store the edited filename or mark a changed option, then submit an enabled confirmation button.
// A missing edit control leaves the current filename intact.
u8 BdFileDialog::ChangeEntry(WnWindow *window, u32 identifier) {
  if (identifier != kBdFileNameEdit) {
    if (identifier != kBdFileBuildingNameEdit && identifier != kBdFileDescriptionEdit)
      return 0;
    field_0d7 = 1;
    return 1;
  }
  WnTextControl *control = nullptr;
  if (window->GetChildAs(identifier, IID_WnTextControl, reinterpret_cast<void **>(&control))) {
    ScString text;
    {
      ScString empty("");
      field_09c = empty;
    }
    if (control->GetText(&text) > 0)
      field_09c = text;
    control->Release();
  }
  if (field_09c.Strlen()) {
    WnWindow *button = window->GetChildWindowFromID(kBdFileAcceptButton);
    if (button && button->GetFlag(kWnFlagEnabled))
      return Commit(window, kBdFileAcceptButton);
  }
  return 1;
}

// Resolve the selected list entry and import it when its leading type byte identifies a building entry.
// The original always returns zero, including after a successful import.
u8 BdFileDialog::SelectEntry(WnWindow *window) {
  WnListControl *list = nullptr;
  if (window->GetChildAs(kBdFileList, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    ScString selected;
    entry_cursor = entries->next;
    i32 index = 0;
    if (list->GetSelection() >= 0) {
      do {
        BdFileEntry *entry = entry_cursor;
        entry_cursor = entry->next;
        selected = entry->value;
        ++index;
      } while (index <= list->GetSelection());
    }
    list->Release();
    if (selected.Strlen()) {
      i8 kind = static_cast<i8>(selected.Data()[0]);
      if ((kind >= 18 && kind <= 19) || kind == 30)
        ImportEntry(window, 1);
    }
  }
  return 0;
}

// Activate the selected file-list entry, either committing a building or navigating an eligible folder.
// Save-oriented dialog modes request the alternate import behavior for folders.
u8 BdFileDialog::ActivateEntry(WnWindow *window, u32) {
  u8 result = 0;
  WnListControl *list = nullptr;
  if (window->GetChildAs(kBdFileList, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    ScString selected;
    entry_cursor = entries->next;
    i32 index = 0;
    if (list->GetSelection() >= 0) {
      do {
        BdFileEntry *entry = entry_cursor;
        entry_cursor = entry->next;
        selected = entry->value;
        ++index;
      } while (index <= list->GetSelection());
    }
    list->Release();
    if (selected.Strlen()) {
      i8 kind = static_cast<i8>(selected.Data()[0]);
      if ((kind >= 18 && kind <= 19) || kind == 30)
        result = Commit(window, kBdFileAcceptButton);
      else if (kind == 6 || kind == 12 || (kind >= 14 && kind <= 15) || kind == 17)
        ImportEntry(
            window, mode != kBdFileSaveBuilding && mode != kBdFileSaveBuildingSet && mode != kBdFileRenderSetDirectory);
    }
  }
  return result;
}

// Find the extension paired with the requested entry type and copy it to the caller's string.
// Each filter stores its type in the first byte of its second string.
bool BdFileDialog::FindExtension(i32 kind, ScString *out) {
  for (u32 i = 0; i < filter_count; ++i) {
    if (static_cast<i8>(filters[i].second.Data()[0]) == kind) {
      *out = filters[i].first;
      return true;
    }
  }
  return false;
}

// Lowercase the supplied extension in place and find its matching file-type entry.
// A match returns the first type byte sign-extended to 32 bits; an unknown extension returns zero.
i32 BdFileDialog::FindKind(ScString *extension) {
  extension->MakeLower();
  for (u32 i = 0; i < filter_count; ++i) {
    ScString candidate(filters[i].first);
    if (extension->Strlen() == candidate.Strlen() && !memcmp(extension->Data(), candidate.Data(), extension->Strlen()))
      return static_cast<i8>(filters[i].second.Data()[0]);
  }
  return 0;
}

// Rebuild the drive hierarchy and file list from the dialog's current directory and selected mode.
// The retained entry strings carry the icon byte used by selection and activation handlers.
u8 BdFileDialog::RefreshFileList(WnWindow *window) {
  if (field_0d4)
    return 0;
  field_0d4 = 1;
  WnComboControl *combo = nullptr;
  if (!window->GetChildAs(kBdFileDirectoryCombo, IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  combo->Release();
  WnListControl *list = nullptr;
  if (!window->GetChildAs(kBdFileList, IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  list->Release();
  FtFont *font = BdGetWindowResources()->GetFont(kWnFontUiFileDialog);
  field_034.Erase(0, field_034.Strlen());
  search = new BdDirectorySearch(&field_060, 1, 0, 0);
  combo = nullptr;
  if (!window->GetChildAs(kBdFileDirectoryCombo, IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  ScString text;
  i32 combo_index = 0;
  while (combo->GetStringFromIndex(combo_index, &text) == 1) {
    if (text.Strlen() && text.Data()[0] == ' ')
      combo->RemoveString(combo_index);
    else
      ++combo_index;
  }
  combo_index = 0;
  while (combo->GetStringFromIndex(combo_index, &text) == 1) {
    u32 offset = text.Strlen() ? 1 : 0;
    while (offset < text.Strlen() && text.Data()[offset] == ' ')
      ++offset;
    if (!ScCompareNoCase(text.Data() + offset, field_060.Data(), 1)) {
      BdPath path(&field_060);
      ScString directory(*path.GetDirectory());
      if (directory.Strlen() && directory.Data()[0] == BdNativeSeparator)
        directory.Erase(0, 1);
      u32 depth = 0;
      while (directory.Strlen()) {
        const char *separator = static_cast<const char *>(memchr(directory.Data(), BdNativeSeparator, directory.Strlen()));
#if defined(_WIN32)
        u32 length = separator ? static_cast<u32>(separator - directory.Data()) : 0;
#else
        u32 length = separator ? static_cast<u32>(separator - directory.Data()) : directory.Strlen();
#endif
        ScString component(directory.Data(), length), label;
        if (separator)
          directory.Erase(0, length + 1);
#if !defined(_WIN32)
        else
          directory.Erase(0, length);
#endif
        ++depth;
        for (u32 i = 0; i < depth; ++i)
          label.Append("     ");
        const char icon = 6;
        label.Append(&icon, 1);
        label.Append(" ");
        label.Append(component.Data(), component.Strlen());
        combo_index = static_cast<i32>(static_cast<u32>(combo_index) + 1);
        combo->InsertString(label, combo_index);
      }
      combo->SetSelection(combo_index, true);
      break;
    }
    ++combo_index;
  }
  combo->Release();
  list->RemoveAllStrings();
  BdFileEntry *entry = entries->next;
  while (entry != entries) {
    BdFileEntry *next = entry->next;
    delete entry;
    entry = next;
  }
  entries->next = entries;
  entries->previous = entries;
  ScString wildcard("*.*");
  search->SetPattern(&wildcard, 1);
  BdDirectoryNames directories, files;
  search->GetNames(&directories, 4);
  directories.Sort();
  BdPath current(&field_060);
  i32 index = 0, first_file = 0;
  if (current.GetDirectory()->Strlen() > 1) {
    const char icon = 12;
    ScString value(&icon, 1);
    value.Append(" ");
    BdFileEntry *node = new BdFileEntry;
    node->value = value;
    node->next = entries;
    node->previous = entries->previous;
    entries->previous->next = node;
    entries->previous = node;
    list->InsertString(value, index++);
  }
  for (u32 i = 0; i < directories.names.size; ++i) {
    ScString value(*directories.names.data[i]), label(value);
    BdFitDialogText(&label, 168, font);
    const char prefix[] = {6, ' '};
    label.Replace(0, 0, prefix, 2);
    value.Replace(0, 0, prefix, 2);
    list->InsertString(label, index++);
    BdFileEntry *node = new BdFileEntry;
    node->value = value;
    node->next = entries;
    node->previous = entries->previous;
    entries->previous->next = node;
    entries->previous = node;
  }
  const char *pattern = nullptr;
  if (mode == kBdFileOpenBuilding || mode == kBdFileSaveBuilding)
    pattern = "*.bld";
  else if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet || mode == kBdFileSelectSetForRendering)
    pattern = "*.bst";
  else if (mode == kBdFileRenderSetDirectory)
    pattern = "*.scs";
  if (pattern) {
    ScString filter(pattern);
    search->SetPattern(&filter, 1);
    if (search->entries.head->next != search->entries.head)
      search->GetNames(&files, 8);
  }
  files.Sort();
  for (u32 i = 0; i < files.names.size; ++i) {
    BdPath path(files.names.data[i]);
    ScString extension(*path.GetExtension());
    if (extension.Strlen() && extension.Data()[0] == '.')
      extension.Erase(0, 1);
    i32 kind = FindKind(&extension);
    ScString value, label(*path.GetName());
    if (kind) {
      char icon = static_cast<char>(kind);
      value.Append(&icon, 1);
      value.Append(" ");
    }
    value.Append(path.GetName()->Data(), path.GetName()->Strlen());
    BdFileEntry *node = new BdFileEntry;
    node->value = value;
    node->next = entries;
    node->previous = entries->previous;
    entries->previous->next = node;
    entries->previous = node;
    BdFitDialogText(&label, 168, font);
    const char prefix[] = {static_cast<char>(kind), ' '};
    label.Replace(0, 0, prefix, 2);
    list->InsertString(label, index);
    if (!first_file)
      first_file = index;
    ++index;
  }
  if (mode == kBdFileOpenBuilding || mode == kBdFileOpenBuildingSet || mode == kBdFileSelectSetForRendering)
    list->SetSelection(first_file, true);
  delete search;
  field_0d4 = 0;
  search = nullptr;
  return 1;
}

// Update the filename or secondary text from an edit control and maintain the confirmation state.
// Open modes match the typed name against the current list; save modes trim spaces and append their extension.
bool BdFileDialog::EditEntry(WnWindow *window, u32 identifier) {
  if (identifier != kBdFileNameEdit && identifier != kBdFileBuildingNameEdit)
    return identifier == kBdFileBuildingTypeControl;
  WnTextControl *edit = nullptr;
  if (!window->GetChildAs(identifier, IID_WnTextControl, reinterpret_cast<void **>(&edit)))
    return false;
  ScString text;
  if (edit->GetText(&text) <= 0) {
    if (identifier == kBdFileNameEdit)
      BdSetDialogChildEnabled(window, kBdFileAcceptButton, 0);
  } else if (identifier == kBdFileNameEdit) {
    field_09c = text;
    field_00c = field_060;
    if (mode == kBdFileSaveBuilding || mode == kBdFileSaveBuildingSet || mode == kBdFileRenderSetDirectory) {
      u32 length = field_09c.Strlen(), first = 0;
      while (first < length && field_09c.Data()[first] == ' ')
        ++first;
      if (first)
        field_09c.Erase(0, first);
      length = field_09c.Strlen();
      while (length && field_09c.Data()[length - 1] == ' ')
        --length;
      if (length != field_09c.Strlen())
        field_09c.Erase(length, field_09c.Strlen() - length);
    }
    field_00c.Append(field_09c.ToChar());
    bool enabled = true;
    if (mode == kBdFileOpenBuilding || mode == kBdFileOpenBuildingSet || mode == kBdFileSelectSetForRendering) {
      enabled = false;
      WnWindow *child = window->GetChildWindowFromID(kBdFileList);
      WnListControl *list = nullptr;
      if (child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list))) {
        ScString entry_text, candidate;
        i32 index = 0;
        while (index < list->GetItemCount()) {
          entry_cursor = entries->next;
          for (i32 current = 0; current <= index; ++current) {
            BdFileEntry *entry = entry_cursor;
            entry_cursor = entry->next;
            entry_text = entry->value;
          }
          u32 length = entry_text.Strlen();
          if (length > 2) {
            ScString suffix(entry_text.Data() + 2, length - 2);
            candidate = suffix;
          } else {
            ScString empty("");
            candidate = empty;
          }
          if (candidate.Strlen() == text.Strlen() &&
              ScCompareNoCase(candidate.Data(), text.Data(), candidate.Strlen()) == 0) {
            enabled = true;
            break;
          }
          ++index;
        }
        list->Release();
      }
    }
    BdSetDialogChildEnabled(window, kBdFileAcceptButton, enabled);
    i32 kind;
    if (mode == kBdFileOpenBuilding || mode == kBdFileSaveBuilding)
      kind = 18;
    else if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet || mode == kBdFileSelectSetForRendering)
      kind = 30;
    else
      kind = mode == kBdFileRenderSetDirectory ? 19 : 18;
    ScString extension;
    if (FindExtension(kind, &extension)) {
      field_00c.Append(".");
      field_00c.Append(extension.Data(), extension.Strlen());
    }
  } else {
    field_088 = text;
  }
  edit->Release();
  return true;
}

// Rebuild the current directory from the selected combo entry and its preceding parent entries.
// Refresh the file list before clearing the change guard and reevaluating the filename control.
u8 BdFileDialog::Populate(WnWindow *window) {
  ScString text;
  if (field_0d5)
    return 0;
  field_0d5 = 1;
  WnComboControl *combo = nullptr;
  if (!window->GetChildAs(kBdFileDirectoryCombo, IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  ScString path;
  i32 selected = combo->GetSelection();
  u8 kind;
  do {
    combo->GetStringFromIndex(selected, &text);
    u32 length = text.Strlen(), first = 0;
    while (first < length && text.Data()[first] == ' ')
      ++first;
    kind = first < length ? static_cast<u8>(text.Data()[first]) : 0;
    ScString part;
    if (first + 1 < length) {
      ScString suffix(text.Data() + first + 1, length - first - 1);
      part = suffix;
    }
    u32 spaces = 0;
    while (spaces < part.Strlen() && part.Data()[spaces] == ' ')
      ++spaces;
    if (spaces)
      part.Erase(0, spaces);
    if (path.Strlen()) {
#if !defined(_WIN32)
      if (part.Strlen() != 1 || part.Data()[0] != '/')
#endif
        part.Append(BdNativeSeparatorString);
      part.Append(path.Data(), path.Strlen());
    }
    path = part;
    --selected;
  } while (kind == 6);
  if (!path.Strlen() || path.Data()[path.Strlen() - 1] != BdNativeSeparator)
    path.Append(BdNativeSeparatorString);
  field_060 = path;
  RefreshFileList(window);
  field_0d5 = 0;
  combo->Release();
  EditEntry(window, kBdFileNameEdit);
  return 1;
}

// The original leaves its reentry flag set when a required control lookup fails.
u8 BdFileDialog::ImportEntry(WnWindow *window, u8 open) {
  ScString selected, filename;
  if (field_0d5)
    return 0;
  field_0d5 = 1;
  WnWindow *child = window->GetChildWindowFromID(kBdFileList);
  if (!child)
    return 0;
  WnListControl *list = nullptr;
  if (!child->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  entry_cursor = entries->next;
  i32 index = 0;
  if (list->GetSelection() >= 0) {
    do {
      BdFileEntry *entry = entry_cursor;
      entry_cursor = entry->next;
      selected = entry->value;
      ++index;
    } while (index <= list->GetSelection());
  }
  list->Release();
  child = window->GetChildWindowFromID(kBdFileNameEdit);
  if (!child)
    return 0;
  WnTextControl *edit = nullptr;
  if (!child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&edit)))
    return 0;
  edit->GetText(&filename);
  edit->Release();
  i8 kind = static_cast<i8>(selected.Data()[0]);
  if (kind == 6 || kind == 12) {
    if (kind == 6) {
      u32 length = selected.Strlen();
      ScString part(length > 1 ? selected.Data() + 1 : "", length > 1 ? length - 1 : 0);
      field_034 = part;
      u32 spaces = 0;
      while (spaces < field_034.Strlen() && field_034.Data()[spaces] == ' ')
        ++spaces;
      if (spaces)
        field_034.Erase(0, spaces);
      field_034.Append(BdNativeSeparatorString);
      field_020.Append(BdNativeSeparatorString);
      field_020.Append(field_034.ToChar());
      field_060.Append(field_034.ToChar());
    } else if (field_060.Strlen() > 1) {
      field_060.Erase(field_060.Strlen() - 1, 1);
      while (field_060.Strlen() && field_060.Data()[field_060.Strlen() - 1] != BdNativeSeparator)
        field_060.Erase(field_060.Strlen() - 1, 1);
    }
    RefreshFileList(window);
    if (mode != kBdFileSaveBuilding) {
      WnWindow *current = window->GetChildWindowFromID(kBdFileList);
      WnListControl *updated = nullptr;
      if (current->QueryInterface(IID_WnListControl, reinterpret_cast<void **>(&updated))) {
        i32 selection = updated->GetSelection();
        updated->GetStringFromIndex(selection, &selected);
        updated->Release();
        kind = static_cast<i8>(selected.Data()[0]);
        if ((kind >= 18 && kind <= 19) || kind == 30) {
          u32 length = selected.Strlen();
          ScString name(length > 2 ? selected.Data() + 2 : "", length > 2 ? length - 2 : 0);
          filename = name;
        } else {
          ScString empty("");
          filename = empty;
        }
      }
    }
  } else if ((kind >= 18 && kind <= 19) || kind == 30) {
    u32 length = selected.Strlen();
    ScString name(length > 2 ? selected.Data() + 2 : "", length > 2 ? length - 2 : 0);
    filename = name;
  }
  if (open)
    edit->SetText(&filename);
  else
    edit->GetText(&filename);
  BdSetDialogChildEnabled(window, kBdFileAcceptButton, filename.Strlen() != 0);
  if (filename.Strlen()) {
    field_00c = field_060;
    if (mode == kBdFileSaveBuilding || mode == kBdFileSaveBuildingSet || mode == kBdFileRenderSetDirectory) {
      u32 length = filename.Strlen(), first = 0;
      while (first < length && filename.Data()[first] == ' ')
        ++first;
      if (first)
        filename.Erase(0, first);
      length = filename.Strlen();
      while (length && filename.Data()[length - 1] == ' ')
        --length;
      if (length != filename.Strlen())
        filename.Erase(length, filename.Strlen() - length);
    }
    field_00c.Append(filename.Data(), filename.Strlen());
    ScString extension;
    if (FindExtension(kind, &extension)) {
      field_00c.Append(".");
      field_00c.Append(extension.Data(), extension.Strlen());
    }
  }
  if (filename.Strlen() && mode == kBdFileOpenBuilding && field_0d6 == 1)
    UpdatePreview(window);
  field_0d5 = 0;
  return 1;
}

// Read a building thumbnail and document, copying its metadata into the original separate outputs.
// A failed archive read clears only the image output, and either result clears the dialog's changed flag.
void BdFileDialog::ReadPreview(const ScString *path, ScString *text_first, ScString *text_second, ScString *text_third,
    u32 *first, u32 *second, GxBuffer **image, u32 *size) {
  BdDocument document;
  GxBuffer *loaded = nullptr;
  if (BdGetManager()->LoadPreview(path->begin, &loaded, &document)) {
    *text_first = document.text_008;
    *text_second = document.text_01c;
    *text_third = document.text_030;
    *first = document.word_044;
    *second = document.word_04c;
    *size = document.byte_004;
    *image = loaded;
  } else {
    *image = nullptr;
  }
  field_0d7 = 0;
}

// Missing archive metadata leaves its otherwise undefined scalar outputs initialized to zero.
void BdFileDialog::UpdatePreview(WnWindow *window) {
  u32 length = field_00c.Strlen();
  if (length == field_074.Strlen() && !memcmp(field_00c.Data(), field_074.Data(), length))
    return;
  WnWindowManager *manager = BdGetWindowManager();
  bool pushed = false;
  if (manager) {
    WnCursorManager *cursors = manager->GetCursorManager();
    if (cursors)
      pushed = cursors->PushAutoCursor(14);
  }
  ScString first, second, third;
  u32 value_first = 0, value_second = 0, size = 0;
  GxBuffer *image = nullptr;
  ReadPreview(&field_00c, &first, &second, &third, &value_first, &value_second, &image, &size);
  if (mode == kBdFileOpenBuilding) {
    WnWindow *child = window->GetChildWindowFromID(kBdFilePreviewBitmap);
    WnBitmapControl *bitmap = nullptr;
    if (child && child->QueryInterface(IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
      bitmap->SetImage(image);
      bitmap->Release();
    }
    child = window->GetChildWindowFromID(kBdFileBuildingNameEdit);
    WnTextControl *text = nullptr;
    if (child && child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&text))) {
      text->SetText(&second);
      text->Release();
    }
    child = window->GetChildWindowFromID(kBdFileDescriptionEdit);
    WnEditorControl *editor_control = nullptr;
    if (child && child->QueryInterface(IID_WnEditorControl, reinterpret_cast<void **>(&editor_control))) {
      editor_control->SetText(third);
      editor_control->Release();
    }
    child = window->GetChildWindowFromID(kBdFileBuildingTypeControl);
    text = nullptr;
    if (child && child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&text))) {
      BdTextResource resource(value_first, kRsArchitectTextGroup);
      if (resource.text)
        text->SetText(static_cast<ScStringView *>(resource.text));
      else {
        ScString empty;
        text->SetText(&empty);
      }
      text->Release();
    }
    child = window->GetChildWindowFromID(kBdFileSizeValue);
    if (child) {
      char dimensions[32];
      snprintf(dimensions, sizeof(dimensions), "%dx%d", static_cast<i32>(size), static_cast<i32>(size));
      ScString caption(dimensions);
      child->SetCaption(caption);
    }
  }
  if (image)
    image->Release();
  field_074 = field_00c;
  if (pushed) {
    manager = BdGetWindowManager();
    if (manager) {
      WnCursorManager *cursors = manager->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
}

static void BdAppendDialogFilter(BdFileDialog *dialog, const char *extension, i8 kind) {
  if (dialog->filter_count == dialog->filter_capacity) {
    u32 next = dialog->filter_count ? dialog->filter_count * 2 : 1;
    BdFileFilter *grown = new BdFileFilter[next];
    for (u32 i = 0; i < dialog->filter_count; ++i) {
      grown[i].first = dialog->filters[i].first;
      grown[i].second = dialog->filters[i].second;
    }
    delete[] dialog->filters;
    dialog->filters = grown;
    dialog->filter_capacity = next;
  }
  BdFileFilter *filter = &dialog->filters[dialog->filter_count++];
  filter->first.FromChar(extension);
  char type = static_cast<char>(kind);
  filter->second.FromChar(&type, 1);
}

static ScString BdDialogTitle(u32 instance, u32 group) {
  BdTextResource resource(instance, group);
  return resource.GetRawText();
}

// Create a localized dialog label while keeping its resource alive through the actual control builder call.
static void BdDialogLocalizedLabel(WnWindow *window, u32 id, i32 x, i32 y, u32 instance) {
  BdTextResource resource(instance, kRsArchitectTextGroup);
  ScString caption = resource.GetRawText();
  BdCreateDialogLabel(window, id, x, y, &caption, 7);
}

// Create a fixed-width localized metadata label and preserve the source's font and alignment settings.
static void BdDialogMetadataLabel(WnWindow *window, u32 id, i32 y, u32 instance) {
  BdTextResource resource(instance, kRsArchitectTextGroup);
  ScString caption = resource.GetRawText();
  BdCreateStyledLabel(window, id, 20, y, 94, 0, &caption, 7, 15, 1);
}

// Measure an existing metadata label, releasing the queried interface before returning its width.
static i32 BdDialogLabelWidth(WnWindow *window, u32 id) {
  WnLabelControl *label = nullptr;
  if (!window->GetChildAs(id, IID_WnLabelControl, reinterpret_cast<void **>(&label)))
    return 0;
  i32 width = label->GetWindow()->GetW();
  label->Release();
  return width;
}

// Attach an outline placeholder for an unavailable preview image through the real window factory.
static void BdDialogOutline(WnWindow *window, u32 id) {
  WnOutlineControl *outline = nullptr;
  if (BdGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
    outline->SetFlags(kWnOutlineInset);
    ScRect bounds = {0, 0, 200, 200};
    outline->GetWindow()->SetArea(bounds);
    outline->GetWindow()->GZWinMoveTo(17, 70);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    outline->GetWindow()->SetID(id);
    window->ChildAdd(outline->GetWindow());
    outline->Release();
  }
}

// Build the selected architect file dialog, including its directory list, metadata controls and preview image.
// Resource and control creation failures retain the original early exits and ownership ordering where defined.
u8 BdFileDialog::BuildContent(WnWindow *window, u32 requested_mode) {
  if (!requested_mode)
    return mode != 0;
  u32 title_id, title_group = kRsArchitectTextGroup;
  switch (requested_mode) {
  case kBdFileOpenBuilding:
    title_id = 49;
    title_group = kRsArchitectSupplementalTextGroup;
    break;
  case kBdFileOpenBuildingSet:
    title_id = 422;
    break;
  case kBdFileSaveBuilding:
    title_id = 421;
    break;
  case kBdFileSaveBuildingSet:
    title_id = 423;
    break;
  case kBdFileSelectSetForRendering:
    title_id = 427;
    break;
  case kBdFileRenderSetDirectory:
    title_id = 428;
    break;
  default:
    return 0;
  }
  {
    ScString title = BdDialogTitle(title_id, title_group);
    window->SetCaption(title);
  }
  mode = requested_mode;
  field_0d6 = 1;
  field_0d5 = field_0d4 = 0;
  ScString empty;
  BdAppendDialogFilter(this, ".bld", 18);
  BdAppendDialogFilter(this, ".bst", 30);
  BdAppendDialogFilter(this, ".scs", 19);
  FtFont *font = BdGetWindowResources()->GetFont(kWnFontUiFileDialog);
  bool building = mode == kBdFileSaveBuilding || mode == kBdFileOpenBuilding;
  BdCreateDialogButton(window, kBdFileAcceptButton, 425, building ? 378 : 330, kRsStandardButtonImageId, 48);
  BdSetDialogChildEnabled(window, kBdFileAcceptButton, 0);
  BdDialogLocalizedLabel(window, kBdFileDirectoryLabel, 242, 40, 460);
  BdDialogLocalizedLabel(window, kBdFileNameLabel, 242, 275, 461);
  BdCreateDialogInput(window, kBdFileNameEdit, 252, 275, 214, 19, &field_09c, 1);

  WnListControl *list = nullptr;
  if (BdGetCom()->GetClassObject(kWnListClassId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    list->SetHighlightFillAlpha(1.0f);
    list->SetHighlightColor(BdGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
    list->SetFont(font);
    list->GetWindow()->SetID(kBdFileList);
    list->GetWindow()->SetFillColor(255, 255, 255);
    list->GetWindow()->SetArea(252, 70, 466, 270);
    if (!window->ChildAdd(list->GetWindow())) {
      list->GetWindow()->Shutdown();
      list->Release();
      return 0;
    }
  }
  ResolvePath(window, font);

  if (building) {
    bool editable = mode == kBdFileSaveBuilding;
    BdDialogMetadataLabel(window, kBdFileArchitectLabel, 312, 463);
    BdDialogMetadataLabel(window, kBdFileBuildingTypeLabel, 336, 471);
    BdDialogMetadataLabel(window, kBdFileDescriptionLabel, 360, 470);
    u32 first_width = static_cast<u32>(BdDialogLabelWidth(window, kBdFileArchitectLabel));
    u32 second_width = static_cast<u32>(BdDialogLabelWidth(window, kBdFileBuildingTypeLabel));
    u32 third_width = static_cast<u32>(BdDialogLabelWidth(window, kBdFileDescriptionLabel));
    u32 widest = first_width > second_width ? first_width : second_width;
    if (third_width > widest)
      widest = third_width;
    i32 text_x = static_cast<i32>(widest + 25);
    u32 selected_type = 535;
    BdGetLanguageManager()->GetCurrentLanguage();
    if (editable) {
      BdGrid *grid = BdGetCurrentGrid();
      field_088 = grid->localized_465;
      field_09c = grid->localized_466;
      field_0b0 = grid->field_038;
      if (grid->field_04c)
        selected_type = grid->field_04c;
      BdCreateDialogFrame(window, text_x, 336, 300, 20);
      WnComboControl *combo = nullptr;
      if (BdGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
        WnWindow *combo_window = combo->GetWindow();
        combo_window->SetID(kBdFileBuildingTypeControl);
        u32 selected = 0;
        for (u32 i = 0; i < available_types.size; ++i) {
          ScString value;
          {
            BdTextResource resource(available_types.data[i], kRsArchitectTextGroup);
            value = resource.GetRawText();
          }
          if (combo->InsertString(value, -2) && available_types.data[i] == selected_type)
            selected = i;
        }
        combo_window->GZWinMoveTo(text_x + 2, 338);
        combo_window->SetSize(298, 18);
        combo->SetFont(BdGetWindowResources()->GetFont(kWnFontUiSmall));
        combo->SetEditable(0);
        window->ChildAdd(combo_window);
        combo->SetSelection(static_cast<i32>(selected), false);
        combo_window->Release();
      }
    } else {
      BdCreateDialogInput(window, kBdFileBuildingTypeControl, text_x, 336, 300, 20, &empty, 0);
    }
    BdCreateDialogInput(window, kBdFileBuildingNameEdit, text_x, 312, 168, 19, &field_088, static_cast<u8>(editable));
    BdCreateDialogEditor(window, kBdFileDescriptionEdit, text_x, 360, 300, 57, &field_0b0, static_cast<u8>(editable));

    GxBuffer *image = nullptr;
    if (editable) {
      BdGetManager()->UpdateThumbnail();
      BdGrid *grid = BdGetCurrentGrid();
      const ScRect *bounds = grid->buffer->GetBufferArea();
      i32 image_width = bounds->right - bounds->left;
      f32 scale = static_cast<f32>(200.0 / image_width);
      image = BdScalePalettedBicubic(scale, grid->buffer, nullptr, nullptr, nullptr);
    } else {
      BdGetGraphics()->CreateBuffer(&image);
      GxColorType format = BdGetGraphics()->GetScreenColorType();
      image->Init(200, 200, format, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      if (image->Lock(GxLockFlags::Fill)) {
        image->FillNoClip(image->ConvertRGBValueToNative16Bit(255, 0, 255));
        image->Unlock(GxLockFlags::Fill);
      }
    }
    if (image) {
      image->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
      i32 image_width = image->Width(), image_height = image->Height();
      WnBitmapControl *bitmap = nullptr;
      if (BdGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
        bitmap->GetWindow()->SetID(kBdFilePreviewBitmap);
        bitmap->GetWindow()->SetArea(0, 0, image_width, image_height);
        bitmap->SetImage(image);
        window->ChildAdd(bitmap->GetWindow());
        bitmap->GetWindow()->GZWinMoveTo(17, 70);
        bitmap->Release();
      }
      image->Release();
    }
    i32 size_x = 17;
    {
      BdTextResource resource(kRsArchitectSupplementalBuildingSizeText, kRsArchitectSupplementalTextGroup);
      if (resource.text) {
        BdCreateStyledLabel(
            window, kBdFileSizeCaption, 17, 275, 40, 19, static_cast<ScStringView *>(resource.text), 0, 15, 1);
        BdSetChildLabelFlag(window, kBdFileSizeCaption, 2, false);
        BdFitChildLabel(window, kBdFileSizeCaption);
        WnWindow *label = window->GetChildWindowFromID(kBdFileSizeCaption);
        if (label)
          size_x = label->GetR() + 4;
      }
    }
    BdCreateStyledLabel(window, kBdFileSizeValue, size_x, 275, 40, 19, &empty, 0, 15, 1);
    BdSetChildLabelFlag(window, kBdFileSizeValue, 2, false);
    BdGrid *grid = BdGetCurrentGrid();
    u32 size = grid ? grid->view_mode : 0;
    WnWindow *label = window->GetChildWindowFromID(kBdFileSizeValue);
    if (label) {
      char dimensions[32];
      snprintf(dimensions, sizeof(dimensions), "%dx%d", static_cast<i32>(size), static_cast<i32>(size));
      ScString caption(dimensions);
      label->SetCaption(caption);
    }
  }

  if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet) {
    GxColorType format(GxColorType::Color::RGB16);
    BdImageResource image(7, kBdDialogImageGroup, GxVramRequest::SystemMemory, format);
    if (image.buffer) {
      WnBitmapControl *bitmap = nullptr;
      if (BdGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
        BdTextResource resource(kRsArchitectSupplementalBuildingArchitectPlusText, kRsArchitectSupplementalTextGroup);
        image.buffer->SetTransparency(BdGetGraphics()->ConvertRGBValueToNative16Bit(255, 0, 255));
        i32 x = static_cast<i32>((252u - image.buffer->Width()) >> 1);
        bitmap->GetWindow()->GZWinMoveTo(x, 120);
        i32 image_height = image.buffer->Height(), image_width = image.buffer->Width();
        bitmap->GetWindow()->SetSize(image_width, image_height);
        bitmap->SetImage(image.buffer);
        window->ChildAdd(bitmap->GetWindow());
        bitmap->Release();
        if (resource.text) {
          ScString caption = resource.GetRawText();
          i32 y = image.buffer->Height();
          BdCreateStyledLabel(window, 0, 5, y + 122, 247, 66, &caption, 3, 16, 1);
          y = image.buffer->Height();
          BdCreateStyledLabel(window, 0, 4, y + 120, 247, 66, &caption, 3, 16, 0);
        }
      }
    }
  } else {
    BdDialogOutline(window, kBdFilePreviewFirstOutlineId);
    BdDialogOutline(window, kBdFilePreviewSecondOutlineId);
    if (mode == kBdFileRenderSetDirectory || mode == kBdFileSelectSetForRendering) {
      BdTextResource resource(
          mode == kBdFileRenderSetDirectory ? 503 : kRsArchitectSelectRenderSetText, kRsArchitectTextGroup);
      if (resource.text)
        BdCreateStyledLabel(window, 0, 17, 70, 200, 200, static_cast<ScStringView *>(resource.text), 4, 15, 1);
    }
  }
  if (mode == kBdFileOpenBuilding && field_0d6 == 1) {
    field_074.Erase(0, field_074.Strlen());
    ImportEntry(window, 1);
  }
  if (mode == kBdFileSaveBuilding) {
    BdGrid *grid = BdGetCurrentGrid();
    field_088 = grid->localized_465;
    field_09c = grid->localized_466;
    field_0b0 = grid->field_038;
    WnTextControl *text = nullptr;
    if (window->GetChildAs(kBdFileNameEdit, IID_WnTextControl, reinterpret_cast<void **>(&text))) {
      text->SetText(&field_09c);
      text->Release();
    }
  }
  window->SetSize(470, building ? 421 : 330);
  ScRect main_area, dialog_area;
  window->GetMainWindow()->GetArea(&main_area);
  window->GetArea(&dialog_area);
  i32 x = main_area.right + dialog_area.left - main_area.left - dialog_area.right;
  i32 y = main_area.bottom + dialog_area.top - main_area.top - dialog_area.bottom;
  window->GZWinMoveTo(x / 2, y / 2);
  // The original dereferences a null list after a failed factory call; do not repeat that undefined read.
  if (!list)
    return 0;
  list->GetWindow()->GetWindowManager()->SetFocus(list->GetWindow());
  list->Release();
  return 1;
}

static ScString BdCommitText(u32 identifier) {
  BdTextResource resource(identifier, kRsArchitectTextGroup);
  return resource.GetRawText();
}

static void BdFormatCommitText(ScString *out, const ScString *format, const ScString *filename) {
  out->Sprintf(format->Data(), filename->Data());
}

static bool BdCommitStringsEqual(const ScString *first, const ScString *second) {
  return first->Strlen() == second->Strlen() && !memcmp(first->Data(), second->Data(), first->Strlen());
}

// Apply the dialog's selected action, validate the save target and update the current grid's descriptive fields.
u8 BdFileDialog::Commit(WnWindow *window, u32 identifier) {
  if (identifier == kBdFileAcceptButton) {
    ScString extension;
    if (mode == kBdFileOpenBuilding || mode == kBdFileSaveBuilding)
      extension.FromChar(".bld");
    else if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet || mode == kBdFileSelectSetForRendering)
      extension.FromChar(".bst");
    else if (mode == kBdFileRenderSetDirectory)
      extension.FromChar(".scs");
    u32 path_length = field_00c.Strlen(), suffix_length = path_length < 4 ? path_length : 4;
    ScString suffix(field_00c.Data() + path_length - suffix_length, suffix_length);
    if (!BdCommitStringsEqual(&suffix, &extension))
      field_00c.Append(extension.Data(), extension.Strlen());
    {
      BdPath path(&field_060);
      ScString directory(*path.GetDrive());
      ScString part(*path.GetDirectory());
      directory.Append(part.Data(), part.Strlen());
      if (!BdDirectoryExists(&directory)) {
        ScString title = BdCommitText(kRsArchitectErrorMessageText),
                 format = BdCommitText(kRsArchitectInvalidFilePathPleaseTryAnotherText), message;
        BdFormatCommitText(&message, &format, &field_09c);
        BdGetWindowManager()->GZMsgBox(message, title, 0x40000, true, 0);
        return 1;
      }
    }
    if (strpbrk(field_09c.Data(), "<>:\"/\\|*?")) {
      ScString title = BdCommitText(kRsArchitectErrorMessageText),
               format = BdCommitText(kRsArchitectTheFileNameIsNotValidReFormatText), message;
      BdFormatCommitText(&message, &format, &field_09c);
      BdGetWindowManager()->GZMsgBox(message, title, 0x40000, true, 0);
      return 1;
    }
    if ((mode == kBdFileSaveBuilding || mode == kBdFileSaveBuildingSet || mode == kBdFileRenderSetDirectory) &&
        BdFileExists(&field_00c)) {
      ScString title = BdCommitText(kRsArchitectErrorMessageText),
               message = BdCommitText(kRsArchitectOverwriteExistingFileText);
      if (BdGetWindowManager()->GZMsgBox(message, title, 65539, true, 0) != kWnYesButtonId)
        return 1;
      if (!BdCanWriteFile(&field_00c)) {
        title = BdCommitText(kRsArchitectErrorMessageText);
        message = BdCommitText(kRsArchitectThisFileIsReadOnlySaveTheText);
        BdGetWindowManager()->GZMsgBox(message, title, 0x40000, true, 0);
        return 1;
      }
    }
    if (mode == kBdFileSaveBuilding) {
      BdGrid *grid = BdGetCurrentGrid();
      if (grid) {
        field_0d7 |= !BdCommitStringsEqual(&grid->localized_465, &field_088) ||
            !BdCommitStringsEqual(&grid->localized_466, &field_09c);
        grid->localized_465 = field_088;
        grid->localized_466 = field_09c;
        WnEditorControl *editor = nullptr;
        if (window->GetChildAs(kBdFileDescriptionEdit, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
          editor->GetText(&field_0b0);
          field_0d7 |= !BdCommitStringsEqual(&grid->field_038, &field_0b0);
          grid->field_038 = field_0b0;
          editor->Release();
        }
        WnComboControl *combo = nullptr;
        if (window->GetChildAs(kBdFileBuildingTypeControl, IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
          i32 index = combo->GetSelection();
          if (index >= 0 && static_cast<u32>(index) < available_types.size)
            grid->field_04c = available_types.data[index];
          combo->Release();
        }
        if (field_0d7)
          grid->field_054 = BdGetLanguageManager()->GetCurrentLanguage();
      }
    }
    Finish(window, identifier);
    return 1;
  }
  if (identifier == kBdFilePreviewToggle) {
    WnButton *button = nullptr;
    if (window->GetChildAs(identifier, IID_WnButton, reinterpret_cast<void **>(&button))) {
      field_0d6 = button->IsChecked();
      if (field_0d6 == 1)
        UpdatePreview(window);
      if (mode == kBdFileSaveBuilding) {
        if (field_0d6 == 1) {
          WnWindow *child = window->GetChildWindowFromID(kBdFileSavePreviewPane);
          if (child)
            child->ShowWindow();
        }
        WnWindow *child = window->GetChildWindowFromID(kBdFilePreviewBitmap);
        if (child)
          child->HideWindow();
      } else if (mode == kBdFileOpenBuilding) {
        WnWindow *child = window->GetChildWindowFromID(kBdFileSavePreviewPane);
        if (child)
          child->HideWindow();
        if (field_0d6 == 1) {
          child = window->GetChildWindowFromID(kBdFilePreviewBitmap);
          if (child)
            child->ShowWindow();
        }
      }
      button->Release();
    }
    return 1;
  }
  if (identifier != kUiFrameCloseButtonId)
    return 0;
  Finish(window, kBdFileCancelButton);
  return 1;
}

// Hide the dialog, perform the selected mode's manager operation and remember its current directory.
// Post the accepted byte to the original modal message destination after the operation finishes.
u8 BdFileDialog::Finish(WnWindow *window, u32 identifier) {
  u8 accepted = 0;
  window->HideWindow();
  if (identifier == kBdFileAcceptButton) {
    if (mode == kBdFileOpenBuilding) {
      BdGrid *grid = nullptr;
      if (BdGetManager()->LoadGrid(field_00c.Data(), &grid) && grid) {
        BdGetManager()->SetGrid(grid, 0);
        grid->Release();
        accepted = 1;
      }
      BdLastBuildingDirectory = field_060;
      ScMessage message(kWnMsg_ArchitectRebuildToolPanel, 1, 0, 0);
      BdGetMessageServer()->MessagePost(message, false);
    } else if (mode == kBdFileSaveBuilding) {
      BdGrid *grid = BdGetCurrentGrid();
      BdGetManager()->SaveGrid(grid, field_00c.Data());
      accepted = 1;
      BdLastBuildingDirectory = field_060;
    } else if (mode == kBdFileOpenBuildingSet) {
      BdGetManager()->SetOperation19(&field_00c);
      accepted = 1;
      BdLastSetDirectory = field_060;
    } else if (mode == kBdFileSaveBuildingSet) {
      BdGetManager()->SetOperation18(&field_00c);
      accepted = 1;
      BdLastSetDirectory = field_060;
    } else if (mode == kBdFileSelectSetForRendering) {
      accepted = 1;
      BdLastSetDirectory = field_060;
    } else if (mode == kBdFileRenderSetDirectory) {
      accepted = 1;
      BdLastSceneDirectory = field_060;
    }
  } else if (identifier != kBdFileCancelButton) {
    return 0;
  }
  window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), accepted);
  return 1;
}

ScString BdLastBuildingDirectory, BdLastSetDirectory, BdLastSceneDirectory;

// Select the dialog's last-used directory, construct its drive combo and attach it to the window.
// Restore the matching drive when possible, then refresh the file list after releasing the combo interface.
u8 BdFileDialog::ResolvePath(WnWindow *window, FtFont *font) {
  ScString drive, remembered;
  if (mode == kBdFileOpenBuilding || mode == kBdFileSaveBuilding)
    remembered = BdLastBuildingDirectory;
  else if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet || mode == kBdFileSelectSetForRendering)
    remembered = BdLastSetDirectory;
  else if (mode == kBdFileRenderSetDirectory)
    remembered = BdLastSceneDirectory;
  if (!remembered.Strlen()) {
    ScString initial;
    const char *directory = BdGetFileSystem()->AppDirectory();
    if (directory) {
      initial.FromChar(directory);
      initial.Append("..\\");
    }
    if (mode == kBdFileOpenBuilding || mode == kBdFileSaveBuilding)
      initial.Append("Buildings\\");
    else if (mode == kBdFileOpenBuildingSet || mode == kBdFileSaveBuildingSet || mode == kBdFileSelectSetForRendering ||
        mode == kBdFileRenderSetDirectory)
      initial.Append("BuildingSets\\");
    BdPath path(&initial);
    path.MakeAbsolute();
    field_060 = *path.GetFull();
  } else {
    field_060 = remembered;
  }
  BdPath path(&field_060);
  if (!path.Exists()) {
    ScString fallback(BdGetFileSystem()->DataDirectory());
    path.AssignAbsolute(&fallback);
    field_060 = *path.GetFull();
  }
#if !defined(_WIN32)
  if (field_060.Strlen() && field_060.Data()[field_060.Strlen() - 1] != '/')
    field_060.Append("/");
#endif
  u32 path_length = field_060.Strlen();
  drive.FromChar(field_060.Data(), path_length ? 1 : 0);
#if defined(_WIN32)
  field_020.FromChar(field_060.Data() + (path_length > 2 ? 2 : path_length), path_length > 2 ? path_length - 2 : 0);
#else
  field_020 = field_060;
#endif
  WnComboControl *combo = nullptr;
  if (BdGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
    combo->SetEditable(0);
    combo->SetFont(font);
    u32 color = combo->GetWindow()->MakeFillColor(0, 0, 255);
    combo->SetHighlightColor(color);
    combo->GetWindow()->SetID(kBdFileDirectoryCombo);
    combo->GetWindow()->SetFillColor(255, 255, 255);
    combo->GetWindow()->SetArea(252, 40, 466, 59);
  }
  BdDirectoryNames names;
  ScArray<u32> kinds;
  BdDirectorySearch *drive_search;
  {
    ScString empty;
    drive_search = new BdDirectorySearch(&empty, 0, 0, 0);
  }
  BdGetLogicalDrives(&names, &kinds);
  delete drive_search;
  for (u32 i = 0; i < names.names.size; ++i) {
    u8 icon = 14;
    if (kinds.data[i] == 2)
      icon = 15;
    else if (kinds.data[i] == 3)
      icon = 17;
    ScString label(reinterpret_cast<const char *>(&icon), 1);
    label.Append(" ");
    u32 length = names.names.data[i]->Strlen();
    label.Append(names.names.data[i]->Data(), length < 2 ? length : 2);
    combo->InsertString(label, static_cast<i32>(i));
  }
  if (!window->ChildAdd(combo->GetWindow())) {
    combo->GetWindow()->Shutdown();
    combo->Release();
    return 0;
  }
#if !defined(_WIN32)
  combo->SetSelection(0, true);
#else
  ScString text, candidate, selected;
  bool found = false;
  for (u32 attempt = 0; attempt < 2 && !found; ++attempt) {
    if (!attempt) {
      selected = drive;
    } else {
      RsFileSystem *filesystem = BdGetFileSystem();
      if (filesystem) {
        const char *directory = filesystem->DataDirectory();
        if (directory)
          selected.FromChar(directory);
      }
      selected.Append("Res\\");
    }
    i32 index = 0;
    while (combo->GetStringFromIndex(index, &text) == 1) {
      u32 length = text.Strlen(), start = length ? 1 : 0;
      while (start < length && text.Data()[start] == ' ')
        ++start;
      candidate.FromChar(text.Data() + start, start < length ? 1 : 0);
      if (!ScCompareNoCase(selected.Data(), candidate.Data(), 1)) {
        combo->SetSelection(index, true);
        field_060 = selected;
        field_060.Append(":");
        field_060.Append(field_020.Data(), field_020.Strlen());
        found = true;
        break;
      }
      ++index;
    }
  }
  if (!found) {
    combo->SetSelection(0, true);
    ScString first;
    combo->GetStringFromIndex(0, &first);
    if (first.Strlen())
      first.Erase(0, 1);
    u32 spaces = 0;
    while (spaces < first.Strlen() && first.Data()[spaces] == ' ')
      ++spaces;
    first.Erase(0, spaces);
    field_060 = first;
  }
#endif
  combo->Release();
  RefreshFileList(window);
  return 1;
}

static void BdUpperIniName(ScString *name) {
  name->MakeUpper();
}

// Remove only ASCII spaces from the requested edges; exclude the source's all-space backwards overread.
static void BdTrimIniName(ScString *name, bool leading, bool trailing) {
  if (leading) {
    u32 count = 0;
    while (count < name->Strlen() && name->begin[count] == ' ')
      ++count;
    if (count)
      name->Erase(0, count);
  }
  if (trailing) {
    u32 length = name->Strlen(), end = length;
    while (end && name->begin[end - 1] == ' ')
      --end;
    if (end != length)
      name->Erase(end, length - end);
  }
}

// Decode pairs of hexadecimal characters while continuing to write bytes after an invalid digit.
// An odd final character consumes the string terminator, as in the source decoder.
static bool BdDecodeIniHex(const char *text, u32 length, u8 *out) {
  bool valid = true;
  for (u32 i = 0; i < length; i += 2) {
    u8 value = 0;
    for (u32 digit = 0; digit < 2; ++digit) {
      char c = text[i + digit];
      u8 nibble;
      if (c >= '0' && c <= '9')
        nibble = static_cast<u8>(c - '0');
      else if (c >= 'A' && c <= 'F')
        nibble = static_cast<u8>(c - 'A' + 10);
      else if (c >= 'a' && c <= 'f')
        nibble = static_cast<u8>(c - 'a' + 10);
      else {
        nibble = 0;
        valid = false;
      }
      value = static_cast<u8>((value << 4) | nibble);
    }
    *out++ = value;
  }
  return valid;
}

struct BdIniEntry {
  ScString name, value;
};

BdIniIndex::~BdIniIndex() {
  Clear();
}

// Delete each section record while retaining the simple backing allocation for a later index rebuild.
void BdIniIndex::Clear() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
  entries.Clear();
}

BdIniFile::BdIniFile() : keep_open(1), indexed(0) {
}

BdIniFile::~BdIniFile() {
  if (file.IsOpen())
    file.Close();
}

// Borrow the reader's actual selected filename without opening it or adding a reference.
const ScString *BdIniFile::GetPath() const {
  return &name;
}

// Invalidate an already loaded section index before replacing the complete selected path.
void BdIniFile::SetPath(const ScString &input) {
  if (indexed)
    Close();
  name = input;
}

// Release the open handle and each section record while retaining the selected filename.
bool BdIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  sections.Clear();
  indexed = 0;
  return true;
}

// Open the concrete file with the source access/share modes and its bounded retry for an existing locked file.
bool BdIniFile::Open(bool write) {
  time_t start = time(nullptr);
  while (!file.Open(write ? 3 : 1, write ? 3 : 2, write ? 0 : 1)) {
    if (RsFile::FileExists(name)) {
      time_t now = time(nullptr);
      SDL_Delay(250);
      if (now - start < 3)
        continue;
    }
    return false;
  }
  return true;
}

// Validate native UTF-8 after assembling chunks, including scalars split across reads; malformed text fails here.
bool BdIniFile::GetFileLine(ScString *out) {
  ScString line;
  u32 position = file.Position(), remaining = file.Size() - position;
  out->FromChar("");
  bool read = false;
  char block[44] = {};
  while (remaining) {
    u32 count = remaining > 40 ? 40 : remaining;
    if (!file.Read(block, count))
      break;
    read = true;
    remaining -= count;
    u32 end = 0;
    while (end < count && block[end] != '\r' && block[end] != '\n')
      ++end;
    bool newline = end != count;
    block[end] = 0;
    if (newline) {
      file.SeekAbsolute(static_cast<i32>(position + end));
      char byte;
      while (file.Read(&byte, 1))
        if (byte != '\r' && byte != '\n') {
          file.SeekRelative(-1);
          break;
        }
    }
    line.Append(block, static_cast<u32>(strlen(block)));
    if (newline || !remaining)
      break;
    position = file.Position();
  }
  if (!ScUtf8Validate(line.Data(), line.Strlen())) {
    out->FromChar("");
    return false;
  }
  *out = line;
  return read;
}

// Clear the output, then read the selected entry as a signed decimal integer.
// A missing entry or an unsuccessful conversion returns false with the output still zero.
bool BdIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString text;
  *out = 0;
  return ReadEntry(section, key, &text) && sscanf(text.Data(), "%d", out) == 1;
}

// Allocate and decode a hexadecimal entry, returning its byte length and transferring ownership on success. Missing
// entries leave the output untouched; invalid text clears it.
u32 BdIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = text.Strlen();
  if (length & 1) {
    *out = nullptr;
    return 0;
  }
  u32 count = length >> 1;
  *out = new u8[count];
  if (!BdDecodeIniHex(text.Data(), length, *out)) {
    delete[] *out;
    *out = nullptr;
    return 0;
  }
  return count;
}

// Decode as much of the hexadecimal entry as fits the supplied byte count.
// An invalid digit returns zero after the original decoder's writes have occurred.
u32 BdIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 capacity) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = capacity * 2, available = text.Strlen();
  if (length > available)
    length = available;
  return BdDecodeIniHex(text.Data(), length, out) ? length >> 1 : 0;
}

// Collect nonempty keys and values from the requested section before invoking callbacks in file order.
// The snapshot allows callbacks to close or change the reader without disturbing the remaining entries.
bool BdIniFile::EnumEntries(const ScString &section, BdIniEnumCallback callback, BdIniEnumContext *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString section_name(section);
  BdUpperIniName(&section_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size ||
      file.SeekAbsolute(static_cast<i32>(sections.entries.data[index]->position)) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString parsed, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  ScArray<BdIniEntry *> entries;
  while (GetFileLine(&line)) {
    BdTrimIniName(&line, true, false);
    if (!line.Strlen() || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    parsed = line.SplitToken("=");
    BdTrimIniName(&parsed, false, true);
    BdTrimIniName(&line, true, true);
    if (parsed.Strlen() && line.Strlen()) {
      BdIniEntry *entry = new BdIniEntry;
      entry->name = parsed;
      entry->value = line;
      *entries.Append() = entry;
    }
  }
  for (u32 i = 0; i < entries.size; ++i)
    callback(entries.data[i]->name, entries.data[i]->value, context);
  if (!keep_open)
    Close();
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
  return true;
}

// Snapshot section names in sorted order before invoking the callback with an empty value.
// Close the reader after callbacks when the caller has disabled the keep-open setting.
bool BdIniFile::EnumSections(BdIniEnumCallback callback, BdIniEnumContext *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString empty;
  ScArray<ScString *> names;
  for (u32 i = 0; i < sections.entries.size; ++i) {
    ScString *section_name = new ScString(sections.entries.data[i]->name);
    u32 position = names.size;
    names.Append();
    while (position && strcmp(names.data[position - 1]->Data(), section_name->Data()) > 0) {
      names.data[position] = names.data[position - 1];
      --position;
    }
    names.data[position] = section_name;
  }
  for (u32 i = 0; i < names.size; ++i)
    callback(*names.data[i], empty, context);
  if (!keep_open)
    Close();
  for (u32 i = 0; i < names.size; ++i)
    delete names.data[i];
  return true;
}

// Look up the uppercased section name in the cached index, loading it first when necessary.
// With keep-open disabled, the original closes and clears the index before looking up the section.
bool BdIniFile::SectionExists(const ScStringView &section) {
  if (!indexed && !LoadSectionNames())
    return false;
  ScString key(section);
  BdUpperIniName(&key);
  if (!keep_open)
    Close();
  for (u32 i = 0; i < sections.entries.size; ++i)
    if (sections.entries.data[i]->name.IsEqual(&key))
      return true;
  return false;
}

bool BdIniFile::SetLeaveFileOpenBetweenOperations(bool value) {
  if (keep_open != static_cast<u8>(value)) {
    keep_open = value;
    if (!value)
      Close();
  }
  return true;
}

// Scan actual line-start positions into a case-folded section index; the first duplicate section retains its position.
bool BdIniFile::LoadSectionNames() {
  sections.Clear();
  if (!file.IsOpen()) {
    file.name = name;
    if (!Open(false))
      return false;
  }
  file.SeekBegin();
  u32 position = file.Position();
  ScString line;
  while (GetFileLine(&line)) {
    BdTrimIniName(&line, true, false);
    if (line.Strlen() && line.begin[0] == '[') {
      u32 end = 1;
      while (end < line.Strlen() && line.begin[end] != ']')
        ++end;
      if (end >= 2 && end < line.Strlen()) {
        ScString key(line.begin + 1, end - 1);
        BdUpperIniName(&key);
        u32 i = 0;
        while (i < sections.entries.size && !sections.entries.data[i]->name.IsEqual(&key))
          ++i;
        if (i == sections.entries.size) {
          BdIniSection *section = new BdIniSection;
          section->name = key;
          section->position = position;
          *sections.entries.Append() = section;
        }
      }
    }
    position = file.Position();
  }
  indexed = 1;
  return true;
}

// Seek the first matching section and return its first case-insensitive key, trimming only ASCII spaces.
// Failed lookups leave the caller's output unchanged; a new section header ends the search.
bool BdIniFile::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  bool found = false;
  if (indexed || LoadSectionNames()) {
    ScString section_name(section), key_name(key);
    BdUpperIniName(&section_name);
    BdUpperIniName(&key_name);
    u32 i = 0;
    while (i < sections.entries.size && !sections.entries.data[i]->name.IsEqual(&section_name))
      ++i;
    if (i < sections.entries.size &&
        file.SeekAbsolute(static_cast<i32>(sections.entries.data[i]->position)) < file.Size()) {
      ScString line;
      if (GetFileLine(&line))
        while (GetFileLine(&line)) {
          BdTrimIniName(&line, true, false);
          if (!line.Strlen() || line.begin[0] == ';')
            continue;
          if (line.begin[0] == '[')
            break;
          u32 equals = 0;
          while (equals < line.Strlen() && line.begin[equals] != '=')
            ++equals;
          ScString parsed;
          if (equals < line.Strlen()) {
            parsed.FromChar(line.begin, equals);
            line.Erase(0, equals + 1);
          }
          BdTrimIniName(&parsed, false, true);
          BdUpperIniName(&parsed);
          if (parsed.IsEqual(&key_name)) {
            BdTrimIniName(&line, true, true);
            *out = line;
            found = true;
            break;
          }
        }
    }
  }
  if (!keep_open)
    Close();
  return found;
}

// Write an entry in place, preserving the remaining file when inserting or replacing a line.
// New files and sections use the original CRLF formatting and update the cached section index.
bool BdIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (!section.Strlen() || !key.Strlen())
    return false;
  ScString header, replacement;
  file.SetName(name);
  if (!RsFile::FileExists(name)) {
    indexed = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(true))
      return false;
    header.Sprintf("[%s]\r\n", section.Data());
    file.Write(header.Data(), header.Strlen());
    replacement.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(replacement.Data(), replacement.Strlen());
    file.Close();
    return true;
  }
  if (!indexed && !LoadSectionNames())
    return false;
  if (file.IsOpen())
    file.Close();
  if (!Open(true))
    return false;
  ScString section_name(section), key_name(key);
  BdUpperIniName(&section_name);
  BdUpperIniName(&key_name);
  u32 index = 0;
  while (index < sections.entries.size && !sections.entries.data[index]->name.IsEqual(&section_name))
    ++index;
  if (index == sections.entries.size) {
    file.SeekEnd();
    u32 position = file.Position();
    header.Sprintf("\r\n[%s]\r\n", section.Data());
    file.Write(header.Data(), header.Strlen());
    replacement.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(replacement.Data(), replacement.Strlen());
    file.Flush();
    BdIniSection *entry = new BdIniSection;
    entry->name = section_name;
    entry->position = position + 2;
    *sections.entries.Append() = entry;
    if (!keep_open)
      Close();
    return true;
  }
  if (file.SeekAbsolute(sections.entries.data[index]->position) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString parsed, current_key, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  while (true) {
    u32 line_start = file.Position();
    if (!GetFileLine(&line))
      break;
    BdTrimIniName(&line, true, false);
    if (!line.Strlen() || line.begin[0] == ';')
      continue;
    bool next_section = line.begin[0] == '[';
    if (!next_section) {
      parsed = line.SplitToken("=");
      current_key = parsed;
      BdTrimIniName(&current_key, false, true);
      BdUpperIniName(&current_key);
      if (!current_key.IsEqual(&key_name))
        continue;
    }
    u32 tail_start = next_section ? line_start : file.Position();
    u32 tail_size = file.Size() - tail_start;
    u8 *tail = new u8[tail_size];
    if (next_section)
      file.SeekAbsolute(tail_start);
    if (file.Read(tail, &tail_size)) {
      file.SeekAbsolute(line_start);
      replacement.Sprintf("%s = %s\r\n", key.Data(), value.Data());
      file.Write(replacement.Data(), replacement.Strlen());
      file.Write(tail, tail_size);
      file.Resize(file.Position());
    }
    delete[] tail;
    indexed = 0;
    if (!keep_open)
      Close();
    return true;
  }
  file.SeekEnd();
  file.Position();
  replacement.Sprintf("%s = %s\r\n", key.Data(), value.Data());
  file.Write(replacement.Data(), replacement.Strlen());
  file.Flush();
  if (!keep_open)
    Close();
  return true;
}

// Invalidate the section index and write the signed number as a decimal INI entry.
// Empty section or key names fail before changing the index.
bool BdIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (!section.Strlen() || !key.Strlen())
    return false;
  indexed = 0;
  ScString text;
  text.Sprintf("%ld", static_cast<long>(value));
  return WriteEntry(section, key, text);
}

// Convert binary bytes to uppercase hexadecimal and write them as an INI entry.
bool BdIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *data, u32 size) {
  if (!size || size > (UINT32_MAX - 1) / 2)
    return false;
  static constexpr char digits[] = "0123456789ABCDEF";
  char *encoded = new char[size * 2 + 1];
  for (u32 i = 0; i < size; ++i) {
    encoded[2 * i] = digits[data[i] >> 4];
    encoded[2 * i + 1] = digits[data[i] & 15];
  }
  encoded[size * 2] = 0;
  ScString text(encoded);
  bool result = WriteEntry(section, key, text);
  delete[] encoded;
  return result;
}

// Copy at most255 path bytes and terminate the output; native truncation retains complete UTF-8 scalars.
static void BdCopyPathPart(char *out, const char *first, const char *last) {
  if (out) {
    usize length = static_cast<usize>(last - first);
    length = ScUtf8Prefix(first, static_cast<u32>(length), length > 255 ? 255 : static_cast<u32>(length));
    memcpy(out, first, length);
    out[length] = 0;
  }
}

// Split an optional drive, final directory and last filename dot into the original separate bounded outputs.
void BdSplitPath(const char *path, char *drive, char *directory, char *name, char *extension) {
  const char *first = path;
  if (*path && path[1] == ':') {
    if (drive) {
      drive[0] = path[0];
      drive[1] = ':';
      drive[2] = 0;
    }
    first += 2;
  } else if (drive) {
    *drive = 0;
  }
  const char *end = first, *file = first, *dot = nullptr;
  while (*end) {
    if (*end == '/' || *end == '\\')
      file = end + 1;
    else if (*end == '.')
      dot = end;
    ++end;
  }
  BdCopyPathPart(directory, first, file);
  if (dot && dot >= file) {
    BdCopyPathPart(name, file, dot);
    BdCopyPathPart(extension, dot, end);
  } else {
    BdCopyPathPart(name, file, end);
    if (extension)
      *extension = 0;
  }
}


// Resolve the current full path after rebuilding it when a mutable component was exposed.
void BdPath::MakeAbsolute() {
  if (!full_valid)
    Rebuild();
  ScPath path(*this);
  path.FullPath();
  const ScString *full = path.Path();
  if (full->Strlen())
    FromChar(full->Data(), full->Strlen());
}

// Replace the mutable full path and resolve it using the original drive-specific working directory.
// GetFull invalidates the component cache before the assigned spelling is normalized.
void BdPath::AssignAbsolute(const ScString *value) {
  ScString *full = GetFull();
  *full = *value;
  MakeAbsolute();
}

// Reading the full path and its components leaves their cache validity unchanged except for lazy rebuilding.
u8 BdPath::Exists() {
  if (!full_valid)
    Rebuild();
  ScString query(*this);
  if (!parts_valid)
    Split();
  if (!name.Strlen() && !extension.Strlen())
    query.Append("*.*");
  return ScDirectory::DoesAnyEntryExistThatMatchesPattern(query);
}

BdPath::BdPath(const ScString *value) : ScString(*value), full_valid(1), parts_valid(0) {
}

// Release the component strings before the inherited full path is destroyed.
BdPath::~BdPath() = default;

// Split the current full path into the original drive, directory, name and extension fields.
// The source splitter bounds each component and leaves the full spelling unchanged.
void BdPath::Split() {
  char drive_text[4], directory_text[256], name_text[256], extension_text[256];
  BdSplitPath(begin, drive_text, directory_text, name_text, extension_text);
  drive.FromChar(drive_text);
  directory.FromChar(directory_text);
  name.FromChar(name_text);
  extension.FromChar(extension_text);
  parts_valid = 1;
}

// Rebuild the full path from its component strings using the source make-path separators.
// Components are already owned, so long paths do not reproduce the source's fixed-buffer overflow.
void BdPath::Rebuild() {
  ScString result;
  if (drive.Strlen() && drive.Data()[0]) {
    result.Append(drive.Data(), 1);
    result.Append(":");
  }
  if (directory.Strlen() && directory.Data()[0]) {
    result.Append(directory.ToChar());
    u32 length = result.Strlen();
    if (result.Data()[length - 1] != '\\' && result.Data()[length - 1] != '/')
      result.Append(BdNativeSeparatorString);
  }
  result.Append(name.ToChar());
  if (extension.Data()[0] && extension.Data()[0] != '.')
    result.Append(".");
  result.Append(extension.ToChar());
  ScString::operator=(result);
  full_valid = 1;
}

// Return the full path after rebuilding it when a component has been exposed for modification.
// The returned mutable string invalidates the cached component fields.
ScString *BdPath::GetFull() {
  if (!full_valid)
    Rebuild();
  parts_valid = 0;
  return this;
}

// Return the mutable drive component after parsing when needed.
// Exposing it invalidates the cached full path.
ScString *BdPath::GetDrive() {
  if (!parts_valid)
    Split();
  full_valid = 0;
  return &drive;
}

// Return the mutable directory component after parsing when needed.
// Exposing it invalidates the cached full path.
ScString *BdPath::GetDirectory() {
  if (!parts_valid)
    Split();
  full_valid = 0;
  return &directory;
}

// Return the mutable filename component after parsing when needed.
// Exposing it invalidates the cached full path.
ScString *BdPath::GetName() {
  if (!parts_valid)
    Split();
  full_valid = 0;
  return &name;
}

// Return the mutable extension component after parsing when needed.
// Exposing it invalidates the cached full path.
ScString *BdPath::GetExtension() {
  if (!parts_valid)
    Split();
  full_valid = 0;
  return &extension;
}

BdTextResource::BdTextResource(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

// Release the retained String while it remains published, then clear the field.
// A failed lookup leaves no owned reference for the destructor.
BdTextResource::~BdTextResource() {
  if (text) {
    static_cast<ScStringView *>(text)->Release();
    text = nullptr;
  }
}

// Copy the retained String into an independent value, reading its length before its data through the real interface.
ScString BdTextResource::GetRawText() {
  if (text)
    return ScString(*static_cast<ScStringView *>(text));
  return ScString("");
}

void BdTextResource::Resolve() {
  if (!BdGetResourceManager()->GetResource(key, IID_ScStringView, &text))
    text = nullptr;
}
