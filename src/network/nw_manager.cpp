#define _CRT_SECURE_NO_WARNINGS
#include "network/nw_manager.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "network/nw_module.h"
#include "network/nw_occupant.h"
#include "network/nw_records.h"
#include "network/nw_rules.h"
#include "network/nw_tool.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Percentage damage retains the rounded Q29 reciprocal of one hundred before the binary scale.
enum NwDamageFractionScale : u32 {
  kNwDamagePercentageQ29Reciprocal = 0x51eb85,
};

// Legacy network import requires terrain, buildings, zoning, underground, tile-bit and miscellaneous planes.
enum NwLegacyImportPrerequisite : u32 {
  kNwLegacyRequiredSourcePlanes = 0x10002f,
};

// Diagonal construction selects separate first, center and last pieces for each corner or accepted probe side.
enum NwDiagonalPiece : i32 {
  kNwDiagonalCornerLowXLowZCenterPiece = 0x3b1702,
  kNwDiagonalCornerLowXLowZFirstPiece = 0x3b1602,
  kNwDiagonalCornerLowXLowZLastPiece = 0x3b1302,
  kNwDiagonalCornerHighXLowZCenterPiece = 0x3b1802,
  kNwDiagonalCornerHighXLowZFirstPiece = 0x3b1502,
  kNwDiagonalCornerHighXLowZLastPiece = 0x3b1400,
  kNwDiagonalCornerLowXHighZCenterPiece = 0x3b1800,
  kNwDiagonalCornerLowXHighZFirstPiece = 0x3b1500,
  kNwDiagonalCornerLowXHighZLastPiece = 0x3b1402,
  kNwDiagonalCornerHighXHighZCenterPiece = 0x3b1700,
  kNwDiagonalCornerHighXHighZFirstPiece = 0x3b1600,
  kNwDiagonalCornerHighXHighZLastPiece = 0x3b1300,
  kNwDiagonalZeroDxNegativeBeforeFirstPiece = 0x3b2500,
  kNwDiagonalZeroDxNegativeBeforeCenterPiece = 0x3b2100,
  kNwDiagonalZeroDxNegativeBeforeLastPiece = 0x3b1d00,
  kNwDiagonalZeroDxNegativeAfterFirstPiece = 0x3b1c02,
  kNwDiagonalZeroDxNegativeAfterCenterPiece = 0x3b2002,
  kNwDiagonalZeroDxNegativeAfterLastPiece = 0x3b2402,
  kNwDiagonalZeroDxPositiveAfterFirstPiece = 0x3b1d02,
  kNwDiagonalZeroDxPositiveAfterCenterPiece = 0x3b2102,
  kNwDiagonalZeroDxPositiveAfterLastPiece = 0x3b2502,
  kNwDiagonalZeroDxPositiveBeforeFirstPiece = 0x3b2400,
  kNwDiagonalZeroDxPositiveBeforeCenterPiece = 0x3b2000,
  kNwDiagonalZeroDxPositiveBeforeLastPiece = 0x3b1c00,
  kNwDiagonalZeroDzPositiveBeforeFirstPiece = 0x3b2302,
  kNwDiagonalZeroDzPositiveBeforeCenterPiece = 0x3b1f02,
  kNwDiagonalZeroDzPositiveBeforeLastPiece = 0x3b1b02,
  kNwDiagonalZeroDzPositiveAfterFirstPiece = 0x3b1a00,
  kNwDiagonalZeroDzPositiveAfterCenterPiece = 0x3b1e00,
  kNwDiagonalZeroDzPositiveAfterLastPiece = 0x3b2200,
  kNwDiagonalZeroDzNegativeBeforeFirstPiece = 0x3b2202,
  kNwDiagonalZeroDzNegativeBeforeCenterPiece = 0x3b1e02,
  kNwDiagonalZeroDzNegativeBeforeLastPiece = 0x3b1a02,
  kNwDiagonalZeroDzNegativeAfterFirstPiece = 0x3b1b00,
  kNwDiagonalZeroDzNegativeAfterCenterPiece = 0x3b1f00,
  kNwDiagonalZeroDzNegativeAfterLastPiece = 0x3b2300,
};

// Reinitializing a rotation output clears only its low byte before the original getter writes it.
enum NwRotationWord : u32 {
  kNwRotationWithoutLowByte = 0xffffff00,
};

// Local tile catalogs reserve a 128-KiB text buffer and leave one byte beyond the maximum read count.
enum NwCatalogScratchSize : u32 {
  kNwCatalogScratchReadBytes = 0x1ffff,
  kNwCatalogScratchBufferBytes = 0x20000,
};

// Construction-confirmation controls identify the affirmative, cancel and body elements and their icon.
enum NwConfirmControlId : u32 {
  kNwConfirmCancelId = 0x12340000,
  kNwConfirmAcceptId = 0x12340001,
  kNwConfirmBodyLabelId = 0x12340010,
  kNwConfirmIconImageId = 0x42e55fd5,
};

// Installed SC3StringsWindow.IXF records in kRsGameTextGroup provide the local construction prompts.
enum NwConfirmTextId : u32 {
  kNwConfirmYesText = 590,
  kNwConfirmNoText = 591,
  kNwConfirmNeighborText = 593,
  kNwConfirmReportTitleText = 594,
  kNwConfirmTunnelText = 595,
  kNwConfirmRemoveNeighborText = 616,
  kNwConfirmTunnelDamageText = 644,
  kNwConfirmBridgeDamageText = 645,
  kNwConfirmBridgeText = 651,
  kNwConfirmPowerPlatformsText = 722,
  kNwConfirmInsufficientFundsText = 736,
};

// Positive construction prices offer Yes/No; informational reports use the original single-button layout.
enum NwConfirmStyle : u32 {
  kNwConfirmConstructionStyle = 1,
  kNwConfirmReportStyle = 2,
};

NwPieceCatalog *NwDiagonalCatalog = nullptr;
NwConversions *NwSimpleConversions[6] = {}, *NwComplexConversions[6] = {};
NwPieceList *NwCollapsePieces = nullptr;
NwPieceList *NwBridgePieces[4] = {};

// Assert the source's signed-char ctype argument domain without adding a game error path.
static bool NwCatalogDigit(char value) {
  i32 character = (i8)value;
  assert(character >= -1);
  return isdigit(character) != 0;
}

// Preserve atoi's defined decimal conversion, asserting its input domain before the original CRT call.
static u32 NwCatalogNumber(const char *digits) {
  u32 value = 0;
  for (const char *digit = digits; *digit; ++digit) {
    u32 next = (u8)*digit - '0';
    assert(value <= ((u32)INT32_MAX - next) / 10);
    value = value * 10 + next;
  }
  return (u32)atoi(digits);
}

// Append one concrete resource key using ordinary owning storage rather than the original vector allocator.
static void NwAppendCatalogKey(NwPieceCatalog *catalog, const RsKey &key) {
  if (catalog->end == catalog->storageEnd) {
    usize count = catalog->begin != catalog->end ? (usize)(catalog->end - catalog->begin) : 0;
    usize capacity = count ? count * 2 : 4;
    if (capacity < count || capacity > SIZE_MAX / sizeof(RsKey))
      ScFatal(SC_ERROR_NOMEM, "Network catalog cannot grow.");
    RsKey *keys = new RsKey[capacity];
    if (count)
      memcpy(keys, catalog->begin, count * sizeof(RsKey));
    delete[] catalog->begin;
    catalog->begin = keys;
    catalog->end = keys + count;
    catalog->storageEnd = keys + capacity;
  }
  *catalog->end++ = key;
}

// Clear bytes one through the end before the genuine existence/open/read/close calls, retaining byte zero's history.
// A successful read returns one regardless of its count; Open and Close return values are deliberately ignored.
u8 NwManager::ReadFile(RsFile *file, char *scratch) {
  memset(scratch + 1, 0, kNwCatalogScratchReadBytes);
  u32 count = kNwCatalogScratchReadBytes;
  u8 result = 0;
  if (file->Exists()) {
    file->Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead);
    result = file->Read(scratch, &count) ? 1 : 0;
  }
  file->Close();
  return result;
}

// Preserve duplicates, ignored prefixes and byte-zero history; clear the other scratch bytes even when reading fails.
// Assert the original WORD-length copy invariant rather than inventing a fatal parser result.
u8 NwManager::GrokFileTileSet(RsFile *file, char *scratch, NwPieceCatalog *catalog) {
  u8 result = 0;
  RsKey key = {kNwTileResourceType, kNwTileResourceGroup, 0};
  if (ReadFile(file, scratch)) {
    for (char *token = strtok(scratch, " ,\n\t"); token; token = strtok(nullptr, " ,\n\t")) {
      usize length = strlen(token), first = 0;
      assert(length <= UINT16_MAX);
      while (first < length && !NwCatalogDigit(token[first]))
        ++first;
      char *suffix = new char[length + 1];
      strcpy(suffix, token + first);
      usize suffixLength = strlen(suffix), end = 0;
      while (end < suffixLength && NwCatalogDigit(suffix[end]))
        ++end;
      char *digits = new char[suffixLength + 1];
      suffix[end] = 0;
      strcpy(digits, suffix);
      if (NwCatalogDigit(*digits)) {
        key.instance = NwCatalogNumber(digits);
        NwAppendCatalogKey(catalog, key);
      }
      delete[] digits;
      delete[] suffix;
    }
    result = 1;
  }
  memset(scratch + 1, 0, kNwCatalogScratchReadBytes);
  return result;
}

// Preserve the source CRT's signed decimal parsing, asserting only the input domain where atoi is defined.
static i32 NwDataNumber(const char *token) {
  const char *digit = token;
  while (isspace((u8)*digit))
    ++digit;
  bool negative = *digit == '-';
  if (*digit == '-' || *digit == '+')
    ++digit;
  u32 value = 0;
  [[maybe_unused]] u32 limit = negative ? kScIntegerSignBit : kCtUnknownCoordinate;
  while (*digit >= '0' && *digit <= '9') {
    u32 next = (u8)*digit++ - '0';
    assert(value <= (limit - next) / 10);
    value = value * 10 + next;
  }
  return atoi(token);
}

// Append the four defined conversion fields using native owning storage, without copying source padding bytes.
static void NwAppendConversion(NwConversions *table, const NwConversion &entry) {
  if (table->end == table->storageEnd) {
    usize count = table->begin != table->end ? (usize)(table->end - table->begin) : 0;
    usize capacity = count ? count * 2 : 4;
    if (capacity < count || capacity > SIZE_MAX / sizeof(NwConversion))
      ScFatal(SC_ERROR_NOMEM, "Network conversions cannot grow.");
    NwConversion *entries = new NwConversion[capacity]{};
    for (usize i = 0; i < count; ++i)
      entries[i] = table->begin[i];
    delete[] table->begin;
    table->begin = entries;
    table->end = entries + count;
    table->storageEnd = entries + capacity;
  }
  *table->end++ = entry;
}

// Append one piece ID to the actual contiguous list using ordinary owning allocation.
static void NwAppendPiece(NwPieceList *table, u32 piece) {
  if (table->end == table->storageEnd) {
    usize count = table->begin != table->end ? (usize)(table->end - table->begin) : 0;
    usize capacity = count ? count * 2 : 4;
    if (capacity < count || capacity > SIZE_MAX / sizeof(u32))
      ScFatal(SC_ERROR_NOMEM, "Network piece list cannot grow.");
    u32 *pieces = new u32[capacity];
    if (count)
      memcpy(pieces, table->begin, count * sizeof(u32));
    delete[] table->begin;
    table->begin = pieces;
    table->end = pieces + count;
    table->storageEnd = pieces + capacity;
  }
  *table->end++ = piece;
}

// Treat the first nonzero decimal as a header gate, then decode packed pairs in order without enforcing its count.
u8 NwManager::GrokFileConvertSets(RsFile *file, char *scratch, NwConversions *conversions) {
  i32 header = 0;
  NwConversion entry = {};
  u8 result = 0;
  if (ReadFile(file, scratch)) {
    for (char *token = strtok(scratch, " ,\n\t"); token; token = strtok(nullptr, " ,\n\t")) {
      if (!header) {
        header = NwDataNumber(token);
      } else if (!entry.fromPiece) {
        NwRuleDecode(NwDataNumber(token), &entry.fromPiece, &entry.fromState);
      } else {
        NwRuleDecode(NwDataNumber(token), &entry.toPiece, &entry.toState);
        NwAppendConversion(conversions, entry);
        entry.fromPiece = entry.toPiece = 0;
      }
    }
    result = 1;
  }
  memset(scratch + 1, 0, kNwCatalogScratchReadBytes);
  return result;
}

// File success alone determines the returned byte; always clear scratch bytes one through the end afterward.
u8 NwManager::GrokFileProtectedSets(RsFile *file, char *scratch, NwPieceList *pieces) {
  i32 header = 0;
  u8 result = 0;
  if (ReadFile(file, scratch)) {
    for (char *token = strtok(scratch, " ,\n\t"); token; token = strtok(nullptr, " ,\n\t"))
      if (!header)
        header = NwDataNumber(token);
      else
        NwAppendPiece(pieces, (u32)NwDataNumber(token));
    result = 1;
  }
  memset(scratch + 1, 0, kNwCatalogScratchReadBytes);
  return result;
}

u32 NwBridgeReadWord(const u32 **cursor, [[maybe_unused]] const u32 *end) {
  assert(*cursor < end);
  return *(*cursor)++;
}

// Copy all five actual counted piece groups before game callbacks can change the source catalog.
// Assert source-vector and fixed-buffer bounds without adding a game failure or termination path.
void NwReadBridgePattern(const u32 *cursor, const u32 *end, NwBridgePattern *pattern) {
  static const u32 kCapacities[5] = {5, 5, 20, 5, 5};
  u32 *groups[5] = {pattern->start, pattern->leading, pattern->span, pattern->trailing, pattern->finish};
  for (u32 group = 0; group < 5; ++group) {
    u32 count = NwBridgeReadWord(&cursor, end);
    assert(count <= kCapacities[group] && usize(end - cursor) >= count);
    pattern->count[group] = count;
    if (count)
      memcpy(groups[group], cursor, count * sizeof(u32));
    cursor += count;
  }
}

void NwValidateBridgeFillers(
    [[maybe_unused]] const NwBridgePattern *pattern, [[maybe_unused]] u32 leading, [[maybe_unused]] u32 trailing) {
  assert((!leading || pattern->count[1]) && (!trailing || pattern->count[3]));
}

void NwBridgeSkipWords(const u32 **cursor, [[maybe_unused]] const u32 *end, u32 count) {
  assert(usize(end - *cursor) >= count);
  *cursor += count;
}

// Assert initialized side data only when successful shaping will consume those piece buffers.
// Empty unused groups and a path with no piece reads remain valid, including absent side records.
void NwValidateBridgePair(const NwBridgePattern *first, const NwBridgePattern *second, u32 start, u32 middle,
    u32 finish, u32 repeats, u32 leading, u32 trailing) {
  if (!start && !(middle && repeats) && !finish && !leading && !trailing)
    return;
  assert(first && second);
  NwValidateBridgeFillers(first, leading, trailing);
  NwValidateBridgeFillers(second, leading, trailing);
  assert((!start || (first->count[0] && second->count[0])) && (!finish || (first->count[4] && second->count[4])) &&
      (!repeats || (middle <= first->count[2] && middle <= second->count[2])));
}

// Scoped image lookup retains only the actual resource interface, not the factory or its options.
class NwImageRef {
public:
  NwImageRef(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~NwImageRef();
  bool Load(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

// Set the shared image factory's options before resolving the requested buffer; release options before the factory.
NwImageRef::NwImageRef(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = NwGetResourceManager();
  RsFactoryInterface *factory;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      Load(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the retained buffer before clearing its live field.
NwImageRef::~NwImageRef() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

bool NwImageRef::Load(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  return manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Copy the localized resource through both original temporary String owners before releasing the text interface.
static void NwCopyPromptText(u32 instance, ScString *out) {
  NwTextRef text(instance, kRsGameTextGroup);
  ScString copy = text.CopyString();
  ScString intermediate(copy);
  *out = intermediate;
}

static void NwFormatPrompt(ScString *out, const ScString &money) {
  out->Sprintf(out->ToChar(), money.ToChar());
}

// Announce modal entry, localize the amount and prompt, then announce exit while the stack callback is still alive.
// Only the affirmative command accepts construction; unavailable language utilities return before modal exit notification.
u8 NwManager::ConfirmConstruction(u32 text, i32 amount) {
  ScMessage entering(kWnMsg_ConstructionPromptOpened);
  NwGetMessageServer()->MessageSend(entering);
  ScString caption, body, money;
  ScLanguageUtility *language = NwGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  NwConfirmStyle style = amount > 0 ? kNwConfirmConstructionStyle : kNwConfirmReportStyle;
  if (!language)
    return 0;
  NwCopyPromptText(text, &body);
  language->MakeMoneyString((i64)amount, &money);
  NwFormatPrompt(&body, money);
  NwCopyPromptText(kNwConfirmReportTitleText, &caption);
  NwConfirmDialog dialog(body, caption, style);
  i32 result = dialog.Show();
  ScMessage leaving(kWnMsg_ConstructionPromptClosed);
  NwGetMessageServer()->MessageSend(leaving);
  return result == kNwConfirmAcceptId;
}

NwConfirmDialog::NwConfirmDialog(const ScString &body, const ScString &caption, u32 style)
    : body(body), caption(caption), style(style), references(0) {
}

NwConfirmDialog::~NwConfirmDialog() {
}

// Publish only Unknown and the actual generic-window callback identity, retaining the callback after publication.
bool NwConfirmDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwConfirmDialog::AddRef() {
  return ++references;
}

// Drop a nonzero reference without deleting the stack owner and return the live count.
u32 NwConfirmDialog::Release() {
  if (references)
    --references;
  return references;
}

// Configure the registered generic window, run it modally, then destroy its live primary before releasing the interface.
i32 NwConfirmDialog::Show() {
  void *object = nullptr;
  if (!NwGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, &object))
    return 0;
  static_cast<UiGenericInterface *>(object)->SetStyle(kUiGenericDraggable, 1);
  static_cast<UiGenericInterface *>(object)->SetStyle(kUiGenericResizable, 0);
  static_cast<UiGenericInterface *>(object)->SetStyle(kUiGenericCloseButton, 0);
  static_cast<UiGenericInterface *>(object)->SetStyle(kUiGenericScroller, 1);
  static_cast<UiGenericInterface *>(object)->SetCallback(this);
  WnWindowManager *manager = NwGetWindowManager();
  WnWindow *window = static_cast<UiGenericInterface *>(object)->GetWindow();
  i32 result = manager->DoModalWin(nullptr, window);
  manager = NwGetWindowManager();
  window = static_cast<UiGenericInterface *>(object)->GetWindow();
  manager->DestroyWindow(window);
  static_cast<UiGenericInterface *>(object)->Release();
  return result;
}

// Forward accepted button commands and build/layout notifications to the actual owning window.
// Repeated geometry calls preserve the original callbacks and live rectangle reads.
bool NwConfirmDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (!window)
    return 0;
  switch (message) {
  case kWnMsg_Command:
    if (first != 1 || (second != kNwConfirmCancelId && second != kNwConfirmAcceptId))
      return 0;
    window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), second);
    return 1;
  case kWnMsg_KeyDown:
  case kWnMsg_Shutdown:
    return 1;
  case kWnMsg_Init:
    return Attach(window);
  case kWnMsg_ClientConstraints:
    if (!first)
      return 0;
    *reinterpret_cast<ScRect *>(first) = {128, 96, 384, 250};
    return 1;
  case kWnMsg_ClientLayout:
    break;
  default:
    return 0;
  }
  const ScRect *rectangle = reinterpret_cast<const ScRect *>(first);
  if (rectangle) {
    i32 bottom = rectangle->bottom;
    WnWindow *accept = window->GetChildWindowFromID(kNwConfirmAcceptId);
    if (style == kNwConfirmReportStyle) {
      if (accept) {
        i32 width = accept->GetW();
        accept->GZWinMoveTo((i32)(250u - width), 119);
      }
    } else {
      if (accept) {
        i32 left = rectangle->left, width = accept->GetW();
        i32 span = (i32)((u32)rectangle->right - left);
        i32 x = (i32)((u32)left + (i32)(2u * span + 2) / 3 - width / 2);
        accept->GetW();
        i32 height = accept->GetH();
        accept->GZWinMoveTo(x, (i32)((u32)rectangle->bottom - height));
        if (accept->GetT() < bottom)
          bottom = accept->GetT();
      }
      WnWindow *cancel = window->GetChildWindowFromID(kNwConfirmCancelId);
      if (cancel) {
        i32 left = rectangle->left, width = cancel->GetW();
        i32 span = (i32)((u32)rectangle->right - left);
        i32 x = (i32)((u32)left + (i32)((u32)span + 1) / 3 - width / 2);
        i32 height = cancel->GetH();
        cancel->GZWinMoveTo(x, (i32)((u32)rectangle->bottom - height));
        if (cancel->GetT() < bottom)
          cancel->GetT();
      }
    }
    WnWindow *label = window->GetChildWindowFromID(kNwConfirmBodyLabelId);
    if (label)
      label->SetArea(65, 39, 235, 114);
  }
  return 1;
}

// Create the source push/radio fallback and preserve its minimum horizontal gutter before the scoped image releases.
static void NwCreatePromptButton(WnButton **out, u32 instance, u32 identifier) {
  const GxColorType format(GxColorType::Color::RGB16);
  NwImageRef image(instance, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
  ScString empty;
  if (image.buffer) {
    WnResources *resources = NwGetWindowResources();
    const ScRect *area = image.buffer->GetBufferArea();
    *out = resources->CreateButton(identifier, empty, image.buffer, area, 0);
  } else
    *out = NwGetWindowResources()->CreateRadioButton(identifier, empty, nullptr, nullptr, 0);
  i32 horizontal, vertical;
  (*out)->GetGutters(&horizontal, &vertical);
  if (horizontal < 15)
    horizontal = 15;
  (*out)->SetGutters((u8)horizontal, (u8)vertical);
}

// Build the original panels, icon, wrapped text and buttons, then center the modal primary within its main window.
// Temporary resource references release before borrowed images are attached, as in the resource-manager-owned source.
u8 NwConfirmDialog::Attach(WnWindow *window) {
  if (!window)
    return 0;
  window->SetCaption(caption);
  ScString unused, labelText;
  WnButton *button = nullptr;
  WnFlatControl *panel = nullptr;
  bool bodyPanelCreated =
      NwGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel));
  ScRect area{};
  if (bodyPanelCreated) {
    panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    panel->SetOutlineFlags(32);
    WnResources *resources = NwGetWindowResources();
    WnWindow *primary = panel->GetWindow();
    primary->SetFillColor(resources->GetColor(kWnColorUiBackground));
    area = {20, 38, 248, 116};
    panel->GetWindow()->SetArea(area);
    if (!window->ChildAdd(panel->GetWindow())) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(panel->GetWindow());
    }
    ScRelease(&panel);
  }
  WnBitmapControl *bitmap = nullptr;
  const GxColorType format(GxColorType::Color::RGB16);
  GxBuffer *icon;
  {
    NwImageRef image(kNwConfirmIconImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    icon = image.buffer;
  }
  if (icon) {
    area = *icon->GetBufferArea();
    if (NwGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
      area.left = (i32)((u32)area.left + 29);
      area.right = (i32)((u32)area.right + 29);
      area.top = (i32)((u32)area.top + 47);
      area.bottom = (i32)((u32)area.bottom + 47);
      bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
      bitmap->GetWindow()->SetArea(area);
      bitmap->SetImage(icon);
      window->ChildAdd(bitmap->GetWindow());
      bitmap->Release();
      bitmap = nullptr;
    }
  }
  WnLabelControl *label = NwGetWindowResources()->CreateLabel(kNwConfirmBodyLabelId, body);
  if (label) {
    FtFont *font = NwGetWindowResources()->GetFont(kWnFontUiMain);
    label->SetFont(font);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetBackground(false);
    label->GetWindow()->SetArea(65, 39, 235, 114);
    if (window->ChildAdd(label->GetWindow()))
      label->SetAlignment(kWnLabelLeftTop);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  WnFlatControl *footer = nullptr;
  if (NwGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&footer))) {
    u32 background = NwGetWindowResources()->GetColor(kWnThemePanelFillColorId);
    u16 border = (u16)NwGetWindowResources()->GetColor(kWnThemeBorderColorId);
    footer->GetWindow()->SetFlag(kWnFlagReorderable, false);
    if (style == kNwConfirmConstructionStyle)
      footer->GetWindow()->SetArea(11, 119, 251, 150);
    else {
      assert(style == kNwConfirmReportStyle);
      footer->GetWindow()->SetArea(11, 118, 251, 153);
    }
    footer->GetWindow()->SetFillColor(background);
    footer->SetOutlineColor(border);
    footer->SetOutlineFlags(14);
    if (!window->ChildAdd(footer->GetWindow())) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(footer->GetWindow());
    }
    footer->Release();
    footer = nullptr;
  }
  WnWindow *main = window->GetMainWindow();
  window->SetSize(256, style == kNwConfirmReportStyle ? 157 : 154);
  i32 mainHeight = main->GetH(), height = window->GetH();
  i32 y = (i32)((u32)mainHeight - height) / 2;
  i32 mainWidth = main->GetW(), width = window->GetW();
  window->GZWinMoveTo((i32)((u32)mainWidth - width) / 2, y);
  if (style != kNwConfirmReportStyle) {
    assert(bodyPanelCreated || icon);
    i32 span = (i32)((u32)area.right - area.left + 1);
    i32 center = (i32)((u32)area.left + (i32)(2u * span) / 3);
    NwCopyPromptText(kNwConfirmYesText, &labelText);
    NwCreatePromptButton(&button, 1, kNwConfirmAcceptId);
    button->SetFont(NwGetWindowResources()->GetFont(kWnFontUiTitle));
    WnWindow *primary = button->GetWindow();
    primary->SetCaption(labelText);
    window->ChildAdd(primary);
    button->Fit();
    height = button->GetImageH();
    width = button->GetImageW();
    primary->GZWinMoveTo((i32)((u32)center - width / 2), (i32)((u32)area.bottom - height));
    ScRelease(&button);
    center = (i32)((u32)area.left + span / 3);
    NwCopyPromptText(kNwConfirmNoText, &labelText);
    NwCreatePromptButton(&button, 1, kNwConfirmCancelId);
    button->SetFont(NwGetWindowResources()->GetFont(kWnFontUiTitle));
    primary = button->GetWindow();
    primary->SetCaption(labelText);
    window->ChildAdd(primary);
    button->Fit();
    height = button->GetImageH();
    width = button->GetImageW();
    primary->GZWinMoveTo((i32)((u32)center - width / 2), (i32)((u32)area.bottom - height));
    ScRelease(&button);
  } else {
    GxBuffer *image;
    {
      NwImageRef resource(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
      image = resource.buffer;
    }
    {
      ScString empty;
      WnResources *resources = NwGetWindowResources();
      assert(image);
      const ScRect *rectangle = image->GetBufferArea();
      button = resources->CreateButton(kNwConfirmAcceptId, empty, image, rectangle, 32);
    }
    if (button) {
      button->SetButtonFlag(kWnButtonShowText, false);
      button->GetWindow()->SetFlag(kWnFlagReorderable, false);
      i32 x = (i32)(250u - button->GetImageW());
      button->GetWindow()->GZWinMoveTo(x, 119);
      window->ChildAdd(button->GetWindow());
      button->Release();
      button = nullptr;
    }
  }
  return 1;
}

i32 NwDragMarkCount;

NwDrag::NwDrag()
    : toolReferences(0), bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
                             {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      doubleWidth(0), layer(nullptr), primary(nullptr), secondary(nullptr), tertiary(nullptr), city(nullptr),
      terrain(nullptr), config92(1), waterRun(3), config100(1), field104(0), field108(0), field112(0), field116(0),
      changes{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}} {
  city = NwGetCity();
  cursorMap = NwGetCityWindow()->GetCellCursorMap();
  u32 halfZ = city->CellCountZ() >> 1, centerX = (city->CellCountX() >> 1) << kCtMicroCoordinateShift;
  changes = {{i32(centerX), i32(halfZ << kCtMicroCoordinateShift), 0},
      {i32(centerX), i32(halfZ << kCtMicroCoordinateShift), 0}};
}

NwDrag::~NwDrag() {
}

// Publish the primary tool identity or its genuine secondary selection view, then retain through the live virtual
// method. Unsupported queries clear the output.
bool NwDrag::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwStroke);
  HANDLE_QUERY_INTERFACE(IID_NwStroke, NwStroke);
  HANDLE_QUERY_INTERFACE(IID_SpSelection, SpSelection);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwDrag::AddRef() {
  return ++toolReferences;
}

// Decrement the primary count with unsigned wrapping and destroy the complete owner at its final reference.
u32 NwDrag::Release() {
  u32 result = --toolReferences;
  if (!result) {
    delete this;
    return 0;
  }
  return result;
}

// Borrow the current city's budget, then publish all configuration fields in original callback/store order.
// The cursor default-color reset is last, allowing its callback to retire the drag without another owner access.
u8 NwDrag::Setup(u32 wide, ScOccupantManager *target, OcIteratorTest *first, OcIteratorTest *second,
    OcIteratorTest *third, ScDirtBag *dirt, u32 policy, u32 water, u8 option, u8 ignore, i32 configuration, u32 price) {
  budget = NwGetCity()->BudgetLayer();
  doubleWidth = u8(wide);
  layer = target;
  secondary = second;
  tertiary = third;
  primary = first;
  terrain = dirt;
  config92 = policy;
  waterRun = water;
  config100 = option;
  ignoreSlope = ignore;
  config144 = configuration;
  tileCost = price;
  cursorMap->SetDefaultColor(0, 35);
  return 1;
}

// Sample four dirt corners after ordered live-city bounds checks, preserving input mutations between callbacks.
// A failed check leaves the caller's signature untouched; all signed differences are stored as their raw byte values.
bool NwDrag::GroundSignature(const CtCoord &position, u8 signature[4]) {
  u32 x = u32(position.x >> kCtMicroCoordinateShift);
  if (x >= city->CellCountX())
    return false;
  u32 z = u32(position.z >> kCtMicroCoordinateShift);
  if (z >= city->CellCountZ())
    return false;
  u8 first, second, third, fourth;
  terrain->GetVertexAltitudeDirt(u32(position.x >> kCtMicroCoordinateShift), z, &first);
  terrain->GetVertexAltitudeDirt(
      u32((position.x >> kCtMicroCoordinateShift) + 1), u32(position.z >> kCtMicroCoordinateShift), &second);
  terrain->GetVertexAltitudeDirt(
      u32((position.x >> kCtMicroCoordinateShift) + 1), u32((position.z >> kCtMicroCoordinateShift) + 1), &third);
  terrain->GetVertexAltitudeDirt(
      u32(position.x >> kCtMicroCoordinateShift), u32((position.z >> kCtMicroCoordinateShift) + 1), &fourth);
  signature[0] = 0;
  signature[1] = u8(second - first);
  signature[2] = u8(third - first);
  signature[3] = u8(fourth - first);
  return true;
}

// Use the rendered terrain-height provider with the same original bounds, sampling and byte-store order.
bool NwDrag::SurfaceSignature(const CtCoord &position, u8 signature[4]) {
  u32 x = u32(position.x >> kCtMicroCoordinateShift);
  if (x >= city->CellCountX())
    return false;
  u32 z = u32(position.z >> kCtMicroCoordinateShift);
  if (z >= city->CellCountZ())
    return false;
  u8 first, second, third, fourth;
  terrain->GetVertexAltitude(u32(position.x >> kCtMicroCoordinateShift), z, &first);
  terrain->GetVertexAltitude(
      u32((position.x >> kCtMicroCoordinateShift) + 1), u32(position.z >> kCtMicroCoordinateShift), &second);
  terrain->GetVertexAltitude(
      u32((position.x >> kCtMicroCoordinateShift) + 1), u32((position.z >> kCtMicroCoordinateShift) + 1), &third);
  terrain->GetVertexAltitude(
      u32(position.x >> kCtMicroCoordinateShift), u32((position.z >> kCtMicroCoordinateShift) + 1), &fourth);
  signature[0] = 0;
  signature[1] = u8(second - first);
  signature[2] = u8(third - first);
  signature[3] = u8(fourth - first);
  return true;
}

// Require identical dirt-corner differences after one successful signature query.
bool NwDrag::GroundFlat(const CtCoord &position) {
  u8 signature[4];
  return GroundSignature(position, signature) && signature[0] == signature[1] && signature[0] == signature[2] &&
      signature[0] == signature[3];
}

// Classify flat rendered terrain independently of the dirt-height provider.
bool NwDrag::SurfaceFlat(const CtCoord &position) {
  u8 signature[4];
  return SurfaceSignature(position, signature) && signature[0] == signature[1] && signature[0] == signature[2] &&
      signature[0] == signature[3];
}

// Admit flat dirt and the original two-unit slopes along either axis, interpreting differences as signed bytes.
bool NwDrag::GroundSlope(const CtCoord &position) {
  u8 signature[4];
  if (!GroundSignature(position, signature))
    return false;
  i32 difference = i32(i8(signature[0])) - i32(i8(signature[2]));
  return difference >= -2 && difference <= 2 &&
      ((signature[0] == signature[1] && signature[2] == signature[3]) ||
          (signature[1] == signature[2] && signature[3] == signature[0]));
}

// Require the original X-directed equal-corner pairs and signed two-unit altitude limit.
bool NwDrag::SlopeX(const CtCoord &position) {
  u8 signature[4];
  if (!GroundSignature(position, signature))
    return false;
  i32 difference = i32(i8(signature[0])) - i32(i8(signature[2]));
  return signature[1] == signature[2] && signature[3] == signature[0] && difference >= -2 && difference <= 2;
}

// Require the distinct Z-directed equal-corner pairs without adding another signature query.
bool NwDrag::SlopeZ(const CtCoord &position) {
  u8 signature[4];
  if (!GroundSignature(position, signature))
    return false;
  i32 difference = i32(i8(signature[0])) - i32(i8(signature[2]));
  return signature[0] == signature[1] && signature[2] == signature[3] && difference >= -2 && difference <= 2;
}

u8 NwDrag::Water(const CtCoord &position) {
  return terrain->IsWater(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift));
}

// Reject incompatible slopes before water and occupant calls; empty cells remain admissible even when water is present.
// Lookup publication remains live through the predicate callback and the following retained-object release.
bool NwDrag::CanEnter(i32 x, i32 z) {
  CtCoord position{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  if (!ignoreSlope && !GroundSlope(position))
    return false;
  bool water = waterRun && terrain->IsWater(u32(x), u32(z)), result = true;
  ScOccupant *occupant = nullptr;
  CtCoord probe{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  if (layer->GetOccupant(probe, &occupant)) {
    result = primary->Test(occupant) && !water;
    occupant->Release();
  }
  return result;
}

// Test flat terrain or the axis selected solely by dx, retaining the original unused dz argument.
// A successful occupied lookup returns the selected predicate's byte effect after releasing its live publication.
u8 NwDrag::CanStep(i32 x, i32 z, i32 dx, i32) {
  CtCoord position{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  if (!ignoreSlope && !GroundFlat(position) && !(dx ? SlopeX(position) : SlopeZ(position)))
    return 0;
  ScOccupant *occupant = nullptr;
  CtCoord probe{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  u8 result = 1;
  if (layer->GetOccupant(probe, &occupant)) {
    OcIteratorTest *test = dx ? secondary : tertiary;
    result = test->Test(occupant);
    occupant->Release();
  }
  return result;
}

// Mark a full fixed-point coordinate only inside terrain bounds and below the signed module-wide cap.
// Rejected coordinates suppress existing XZ duplicates; the cursor callback precedes the live append and count
// increment.
void NwDrag::AddCell(const CtCoord &position, u8 rejected) {
  if (NwDragMarkCount >= 512 ||
      !terrain->InCellBounds(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift)))
    return;
  if (rejected)
    for (CtCoord *point = begin, *last = end; point != last; ++point)
      if (point->x == position.x && point->z == position.z)
        return;
  cursorMap->SetColor(position, rejected ? 36 : 35);
  Append(position);
  NwDragMarkCount = i32(u32(NwDragMarkCount) + 1);
}

// Check tile coordinates before constructing the aligned zero-altitude point, retaining the original separate
// arguments.
// Only rejected duplicates are skipped; successful coloring appends through the inherited real selection storage.
void NwDrag::AddCell(i32 x, i32 z, u8 rejected) {
  if (NwDragMarkCount >= 512 || !terrain->InCellBounds(u32(x), u32(z)))
    return;
  CtCoord position{i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
  if (rejected)
    for (CtCoord *point = begin, *last = end; point != last; ++point)
      if (point->x == position.x && point->z == position.z)
        return;
  cursorMap->SetColor(position, rejected ? 36 : 35);
  Append(position);
  NwDragMarkCount = i32(u32(NwDragMarkCount) + 1);
}

// Normalize signed XZ bounds while replacing both altitude words with the original unspecified-height sentinel.
static void NwDragNormalize(GeBounds *box) {
  if (box->minimum.x > box->maximum.x) {
    i32 value = box->minimum.x;
    box->minimum.x = box->maximum.x;
    box->maximum.x = value;
  }
  if (box->minimum.z > box->maximum.z) {
    i32 value = box->minimum.z;
    box->minimum.z = box->maximum.z;
    box->maximum.z = value;
  }
  box->minimum.altitude = box->maximum.altitude = kCtUnknownCoordinate;
}

// Form the original XZ union, allowing either input to be the destination without losing a later coordinate.
static void NwDragUnion(GeBounds *out, const GeBounds &first, const GeBounds &second) {
  i32 x0 = first.minimum.x < second.minimum.x ? first.minimum.x : second.minimum.x;
  i32 z0 = first.minimum.z < second.minimum.z ? first.minimum.z : second.minimum.z;
  i32 x1 = first.maximum.x > second.maximum.x ? first.maximum.x : second.maximum.x;
  i32 z1 = first.maximum.z > second.maximum.z ? first.maximum.z : second.maximum.z;
  *out = {{x0, z0, kCtUnknownCoordinate}, {x1, z1, kCtUnknownCoordinate}};
}

static CtCoord NwDragPoint(i32 x, i32 z) {
  return {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
}

static i32 NwDragMagnitude(i32 value) {
  return value < 0 ? -value : value;
}

// Build the complete ordered drag preview, retaining wide-lane selection, terrain transitions and live callback
// effects.
u8 NwDrag::DrawLine(const CtCoord &start, const CtCoord &finish, SpSelection **out, GeBounds *changed) {
  bool invalid = false, crossed = false, extension = false, canLevel = true;
  bool slope = false, previousSlope = false, previousWater = false, same;
  i32 sideX = 0, sideZ = 0, originalX = 0, originalZ = 0, dx = 1, dz = 1, priorX = 1, priorZ = 1;
  i32 run = 0, crossings = 0, waterLeft = 0, slopeLeft = 0, zoom, rotation = 0;
  NwPositionList pending;
  i32 width = i32(terrain->CellCountX()), depth = i32(terrain->CellCountZ());
  pending.Clear();
  NwDragMarkCount = 0;
  // A missing view leaves rotation undefined in the binary; zero keeps that excluded path from reading native stack
  // bytes.
  if (NwGetCurrentView())
    NwGetCurrentView()->GetZoomAndRotation(&zoom, &rotation);
  CtCoord first = start, last = finish;
  if (doubleWidth && NwDragMagnitude((last.x >> kCtMicroCoordinateShift) - (first.x >> kCtMicroCoordinateShift)) >= 1 &&
      NwDragMagnitude((last.z >> kCtMicroCoordinateShift) - (first.z >> kCtMicroCoordinateShift)) >= 1) {
    if (NwDragMagnitude((last.x >> kCtMicroCoordinateShift) - (first.x >> kCtMicroCoordinateShift)) < 2) {
      last.x = i32(u32(first.x >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      last.z = i32(u32(last.z >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      last.altitude = 0;
    }
    if (NwDragMagnitude((last.z >> kCtMicroCoordinateShift) - (first.z >> kCtMicroCoordinateShift)) < 2) {
      last.x = i32(u32(last.x >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      last.z = i32(u32(first.z >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      last.altitude = 0;
    }
  }
  same =
      ((u32(first.x) ^ u32(last.x)) & kCtMicroCellMask) == 0 && ((u32(first.z) ^ u32(last.z)) & kCtMicroCellMask) == 0;
  if (doubleWidth) {
    if (same && rotation >= 0 && rotation <= 3) {
      i32 x = first.x >> kCtMicroCoordinateShift;
      if (rotation <= 1)
        x = x ? x - 1 : 1;
      else
        x = u32(x) >= terrain->CellCountX() - 1 ? x - 1 : x + 1;
      last = NwDragPoint(x, first.z >> kCtMicroCoordinateShift);
    }
    i32 offsetX = rotation == 0 || rotation == 1 ? -1 : rotation == 2 || rotation == 3 ? 1 : 0;
    i32 offsetZ = rotation == 0 || rotation == 3 ? -1 : rotation == 1 || rotation == 2 ? 1 : 0;
    i32 x0 = first.x >> kCtMicroCoordinateShift, z0 = first.z >> kCtMicroCoordinateShift,
        x1 = last.x >> kCtMicroCoordinateShift, z1 = last.z >> kCtMicroCoordinateShift;
    if (same) {
      if (offsetX == -1 && x0 == 0)
        offsetX = 1;
      if (offsetZ == -1 && z0 == 0)
        offsetZ = 1;
      if (offsetX == 1 && x0 == width - 1)
        offsetX = -1;
      if (offsetZ == 1 && z0 == depth - 1)
        offsetZ = -1;
    }
    if (NwDragMagnitude(x1 - x0) <= NwDragMagnitude(z1 - z0)) {
      dx = 0;
      dz = z1 - z0 <= 0 ? -1 : 1;
      sideX = offsetX;
      offsetX = 0;
    } else {
      dx = x1 - x0 <= 0 ? -1 : 1;
      dz = 0;
      sideZ = offsetZ;
      offsetZ = 0;
    }
    i32 nextX0 = offsetX + x0, nextZ0 = offsetZ + z0, nextX1 = offsetX + x1, nextZ1 = offsetZ + z1;
    if (nextX0 < 0)
      nextX0 = 0;
    if (nextZ0 < 0)
      nextZ0 = 0;
    if (nextX1 < 0)
      nextX1 = 0;
    if (nextZ1 < 0)
      nextZ1 = 0;
    if (nextX0 > width)
      x0 = width;
    if (nextZ0 > depth)
      z0 = depth;
    if (nextX1 > width)
      nextX1 = width;
    if (nextZ1 > depth)
      nextZ1 = depth;
    if (dz == -1) {
      if (z0 < nextZ0) {
        i32 x = x0, z = z0;
        x0 = nextX0;
        z0 = nextZ0;
        nextX0 = x;
        nextZ0 = z;
      }
      if (nextZ1 > z1) {
        i32 x = x1, z = z1;
        x1 = nextX1;
        z1 = nextZ1;
        nextX1 = x;
        nextZ1 = z;
      }
    }
    if (dz == 1) {
      if (z0 > nextZ0) {
        i32 x = x0, z = z0;
        x0 = nextX0;
        z0 = nextZ0;
        nextX0 = x;
        nextZ0 = z;
      }
      if (nextZ1 < z1) {
        i32 x = x1, z = z1;
        x1 = nextX1;
        z1 = nextZ1;
        nextX1 = x;
        nextZ1 = z;
      }
    }
    if (dx == -1) {
      if (x0 < nextX0) {
        i32 x = x0, z = z0;
        x0 = nextX0;
        z0 = nextZ0;
        nextX0 = x;
        nextZ0 = z;
      }
      if (nextX1 > x1) {
        i32 x = x1, z = z1;
        x1 = nextX1;
        z1 = nextZ1;
        nextX1 = x;
        nextZ1 = z;
      }
    }
    if (dx == 1) {
      if (x0 > nextX0) {
        x0 = nextX0;
        z0 = nextZ0;
      }
      if (nextX1 < x1) {
        x1 = nextX1;
        z1 = nextZ1;
      }
    }
    originalX = priorX = dx;
    originalZ = priorZ = dz;
    first = NwDragPoint(x0, z0);
    last = NwDragPoint(x1, z1);
    if (sideX || sideZ) {
      CtCoord otherFirst =
          NwDragPoint((first.x >> kCtMicroCoordinateShift) + sideX, (first.z >> kCtMicroCoordinateShift) + sideZ);
      CtCoord otherLast =
          NwDragPoint((last.x >> kCtMicroCoordinateShift) + sideX, (last.z >> kCtMicroCoordinateShift) + sideZ);
      invalid = !layer->IsInBounds(first) || !layer->IsInBounds(last) || !layer->IsInBounds(otherFirst) ||
          !layer->IsInBounds(otherLast);
    } else
      invalid = !layer->IsInBounds(first) || !layer->IsInBounds(last);
  } else
    invalid = !layer->IsInBounds(first) || !layer->IsInBounds(last);
  bounds = {first, last};
  GeBounds affected = {first, last};
  NwDragNormalize(&affected);
  i32 minX = affected.minimum.x >> kCtMicroCoordinateShift, minZ = affected.minimum.z >> kCtMicroCoordinateShift;
  i32 maxX = affected.maximum.x >> kCtMicroCoordinateShift, maxZ = affected.maximum.z >> kCtMicroCoordinateShift;
  NwDragNormalize(&changes);
  GeBounds previous = changes;
  cursorMap->Clear(false);
  Clear();
  i32 x = bounds.minimum.x >> kCtMicroCoordinateShift, z = bounds.minimum.z >> kCtMicroCoordinateShift;
  i32 endX = bounds.maximum.x >> kCtMicroCoordinateShift, endZ = bounds.maximum.z >> kCtMicroCoordinateShift;
  invalid = invalid || !terrain->InCellBounds(u32(x), u32(z)) || !terrain->InCellBounds(u32(endX), u32(endZ));
  ScOccupant *occupant = nullptr;
  bool initial = false;
  if (!invalid && (layer->GetOccupant(bounds.minimum, &occupant) || GroundSlope(bounds.minimum))) {
    if (CanEnter(bounds.minimum.x >> kCtMicroCoordinateShift, bounds.minimum.z >> kCtMicroCoordinateShift) &&
        !GroundFlat(bounds.minimum)) {
      slope = true;
      previousSlope = false;
      u8 signature[4];
      GroundSignature(bounds.minimum, signature);
      if (signature[0] == signature[1])
        dx = priorX = 0;
      else
        dz = priorZ = 0;
    }
    ScRelease(&occupant);
    initial = true;
  } else {
    ScRelease(&occupant);
    if (!GroundSlope(bounds.minimum)) {
      GeBounds cell = {
          NwDragPoint(bounds.minimum.x >> kCtMicroCoordinateShift, bounds.minimum.z >> kCtMicroCoordinateShift),
          NwDragPoint(
              (bounds.minimum.x >> kCtMicroCoordinateShift) + 1, (bounds.minimum.z >> kCtMicroCoordinateShift) + 1)};
      GeBounds leveled{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
      u32 levelingCost = 0;
      u8 altitude;
      if (terrain->InCellBounds(
              u32(cell.minimum.x >> kCtMicroCoordinateShift), u32(cell.minimum.z >> kCtMicroCoordinateShift)) &&
          terrain->InCellBounds(
              u32(cell.maximum.x >> kCtMicroCoordinateShift), u32(cell.maximum.z >> kCtMicroCoordinateShift))) {
        if (terrain->GetOptimalLevel(cell, &altitude, &leveled, &levelingCost)) {
          u32 levelX = u32(leveled.minimum.x >> kCtMicroCoordinateShift),
              levelMaxX = u32(leveled.maximum.x >> kCtMicroCoordinateShift);
          u32 levelMaxZ = u32(leveled.maximum.z >> kCtMicroCoordinateShift);
          while (u16(levelX) <= levelMaxX && canLevel) {
            u32 levelZ = u32(leveled.minimum.z >> kCtMicroCoordinateShift);
            while (u16(levelZ) <= levelMaxZ && canLevel) {
              if (terrain->ChangedByLastOperation(u16(levelX), u16(levelZ))) {
                ScOccupant *entry = nullptr;
                CtCoord point = NwDragPoint(u16(levelX), u16(levelZ));
                if (layer->GetOccupant(point, &entry)) {
                  canLevel = false;
                  entry->Release();
                }
              }
              ++levelZ;
            }
            ++levelX;
          }
        } else
          canLevel = false;
      } else
        invalid = true;
    }
    initial = !invalid && canLevel;
  }
  if (waterRun && (!initial || Water(bounds.minimum)))
    initial = false;
  invalid =
      !initial || !CanEnter(bounds.minimum.x >> kCtMicroCoordinateShift, bounds.minimum.z >> kCtMicroCoordinateShift);
  AddCell(x, z, u8(invalid));
  i32 lastX = x, lastZ = z;
  if (doubleWidth) {
    bool allowed = CanEnter(x + sideX, z + sideZ);
    AddCell(x + sideX, z + sideZ, u8(invalid));
    if (!allowed)
      invalid = true;
    if (sideX < 0)
      --minX;
    else if (sideX > 0)
      ++maxX;
    if (sideZ < 0)
      --minZ;
    else if (sideZ > 0)
      ++maxZ;
    if (!same) {
      if (originalX < 0)
        --minX;
      else if (originalX > 0)
        ++maxX;
      if (originalZ < 0)
        --minZ;
      else if (originalZ > 0)
        ++maxZ;
      if (doubleWidth)
        slope = true;
    }
  }
  for (;;) {
    if (!extension && (NwDragMarkCount >= 512 || (x == endX && z == endZ)))
      break;
    if (!extension && (x < minX || z < minZ || x > maxX || z > maxZ)) {
      x = lastX;
      z = lastZ;
      invalid = true;
    }
    i32 remainingX = endX - x, remainingZ = endZ - z;
    if (!invalid && slope) {
      if (!extension) {
        if (dx) {
          if (!dz)
            dx = remainingX <= 0 ? -1 : 1;
        } else
          dz = remainingZ <= 0 ? -1 : 1;
      }
    } else if (invalid || config100) {
      if (NwDragMagnitude(remainingX) == NwDragMagnitude(remainingZ)) {
        if (!priorZ) {
          dx = 0;
          dz = remainingZ <= 0 ? -1 : 1;
        } else {
          dx = remainingX <= 0 ? -1 : 1;
          dz = 0;
        }
      } else if (NwDragMagnitude(remainingX) > NwDragMagnitude(remainingZ)) {
        dx = remainingX <= 0 ? -1 : 1;
        dz = 0;
      } else {
        dx = 0;
        dz = remainingZ <= 0 ? -1 : 1;
      }
    } else if (dx + dz == 2 || ((!dx || x == endX) && (!dz || z == endZ))) {
      if (NwDragMagnitude(remainingX) > NwDragMagnitude(remainingZ)) {
        dx = remainingX <= 0 ? -1 : 1;
        dz = 0;
      } else {
        dx = 0;
        dz = remainingZ <= 0 ? -1 : 1;
      }
    }
    if (!remainingX && !remainingZ && !extension)
      dx = dz = 0;
    for (;;) {
      if ((sideX && dx) || (sideZ && dz))
        crossed = true;
      if (NwDragMagnitude(sideX) && !priorZ && !dz)
        invalid = true;
      if (NwDragMagnitude(sideZ) && !priorX && !dx)
        invalid = true;
      if (dx != priorX || dz != priorZ)
        run = 0;
      else
        ++run;
      x += dx;
      z += dz;
      priorX = dx;
      priorZ = dz;
      bool outside = x < minX || z < minZ || x > maxX || z > maxZ;
      invalid = invalid || outside;
      bool enter = invalid || CanEnter(x, z);
      if (doubleWidth)
        enter = enter && CanEnter(x + sideX, z + sideZ);
      bool step = enter || CanStep(x, z, dx, dz);
      if (doubleWidth)
        step = step && (enter || CanStep(x + sideX, z + sideZ, dx, dz));
      invalid = invalid || !step;
      bool transition = step && !enter;
      if (crossed && transition) {
        transition = false;
        invalid = true;
      }
      bool water = false;
      if (waterRun) {
        CtCoord point = NwDragPoint(x, z);
        water = Water(point) != 0;
        if (doubleWidth && !water) {
          point = NwDragPoint(x + sideX, z + sideZ);
          water = Water(point) != 0;
        }
        if (water)
          invalid = invalid || u32(run) < waterRun;
        if (crossed && water) {
          water = false;
          invalid = true;
        }
      }
      if (!previousWater && water) {
        ++crossings;
        waterLeft = i32(waterRun);
      }
      previousWater = water;
      invalid = invalid || crossings > 1;
      if (!invalid) {
        CtCoord point = NwDragPoint(x, z);
        slope = !(waterRun ? SurfaceFlat(point) : GroundFlat(point));
      } else
        slope = false;
      if (doubleWidth && !slope) {
        CtCoord point = NwDragPoint(x + sideX, z + sideZ);
        slope = !SurfaceFlat(point);
      }
      if (slope)
        invalid = invalid || u32(run) < field104;
      if (crossed && slope) {
        slope = false;
        invalid = true;
      }
      if (previousSlope) {
        CtCoord point = NwDragPoint(x, z);
        if (GroundFlat(point))
          slopeLeft = i32(config92);
      }
      previousSlope = slope;
      bool pendingTransition = transition || water || waterLeft > 0;
      bool continuing = !invalid && (pendingTransition || ((endX != x || endZ != z) && (slope || slopeLeft > 0)));
      if (!water && waterLeft > 0)
        --waterLeft;
      if (!slope && slopeLeft > 0)
        --slopeLeft;
      if (doubleWidth && !same && continuing && x == endX && z == endZ) {
        continuing = false;
        invalid = true;
      }
      if (!outside && (dx || dz)) {
        AddCell(x, z, u8(invalid));
        lastX = x;
        lastZ = z;
        CtCoord point = NwDragPoint(x, z);
        GeBounds cell{point, point};
        NwDragUnion(&affected, cell, affected);
        if (pendingTransition) {
          if (terrain->InCellBounds(u32(x), u32(z)))
            pending.Append(point);
        } else if (!invalid)
          pending.Clear();
        if (doubleWidth) {
          i32 otherX = x + sideX, otherZ = z + sideZ;
          AddCell(otherX, otherZ, u8(invalid));
          point = NwDragPoint(otherX, otherZ);
          cell = {point, point};
          NwDragUnion(&affected, cell, affected);
          if (pendingTransition && terrain->InCellBounds(u32(otherX), u32(otherZ)))
            pending.Append(point);
        }
      }
      if (extension || NwDragMarkCount >= 512 || !continuing) {
        if (doubleWidth && !same) {
          if (extension) {
            extension = false;
            x = endX;
            z = endZ;
          } else if (endX == x && endZ == z) {
            endX += originalX;
            endZ += originalZ;
            extension = true;
            slope = true;
            dx = originalX;
            dz = originalZ;
          }
        }
        break;
      }
    }
  }
  if (crossings > 0)
    for (NwPositionNode *node = pending.head->next; node != pending.head; node = node->next)
      if (terrain->InCellBounds(
              u32(node->position.x >> kCtMicroCoordinateShift), u32(node->position.z >> kCtMicroCoordinateShift)))
        cursorMap->SetColor(node->position, 36);
  bool allowed = false;
  u32 color = 35;
  if (invalid && same && doubleWidth) {
    cursorMap->Clear(false);
    Clear();
    i32 firstX = bounds.minimum.x >> kCtMicroCoordinateShift, firstZ = bounds.minimum.z >> kCtMicroCoordinateShift;
    endX = bounds.maximum.x >> kCtMicroCoordinateShift;
    endZ = bounds.maximum.z >> kCtMicroCoordinateShift;
    if (terrain->InCellBounds(u32(firstX), u32(firstZ)))
      AddCell(firstX, firstZ, u8(invalid));
    if (terrain->InCellBounds(u32(firstX + sideX), u32(firstZ + sideZ)))
      AddCell(firstX + sideX, firstZ + sideZ, u8(invalid));
    if (terrain->InCellBounds(u32(endX), u32(endZ)))
      AddCell(endX, endZ, u8(invalid));
    if (terrain->InCellBounds(u32(endX + sideX), u32(endZ + sideZ)))
      AddCell(endX + sideX, endZ + sideZ, u8(invalid));
  } else {
    NwPositionList removed, positions;
    positions.Clear();
    CtCoord *firstPoint = begin, *lastPoint = end;
    for (CtCoord *point = firstPoint; point != lastPoint; ++point)
      positions.Append(*point);
    i32 processed = 0;
    // The city's4147C2FB publication is the proven same-primary NwManager producer; no query or retain occurs here.
    reinterpret_cast<NwManager *>(NwGetCity()->TransitLayer())
        ->DecomposeRuns(&positions, &removed, config144, 0, &processed);
    firstPoint = begin;
    lastPoint = end;
    u32 index = 0;
    for (CtCoord *point = firstPoint; point != lastPoint; ++point) {
      if (index >= u32(processed))
        cursorMap->SetColor(*point, 36);
      ++index;
    }
    firstPoint = begin;
    lastPoint = end;
    for (CtCoord *point = firstPoint; point != lastPoint; ++point) {
      cursorMap->GetColor(*point, &color);
      allowed = color == 35;
      if (allowed)
        break;
    }
  }
  u32 cost = 0;
  CtCoord *firstPoint = begin, *lastPoint = end;
  for (CtCoord *point = firstPoint; point != lastPoint; ++point) {
    cursorMap->GetColor(*point, &color);
    if (color == 35)
      cost += tileCost;
  }
  i64 funds = budget->GetTotalFunds();
  if (cost && i64(u64(cost)) > funds) {
    firstPoint = begin;
    lastPoint = end;
    for (CtCoord *point = firstPoint; point != lastPoint; ++point)
      cursorMap->SetColor(*point, 36);
  }
  GeBounds region = affected;
  NwDragUnion(&region, previous, affected);
  cursorMap->UpdateRegion(region);
  *changed = region;
  changes = affected;
  QueryInterface(IID_SpSelection, reinterpret_cast<void **>(out));
  return u8(allowed);
}

// Truncate both tile words before bounds and occupant lookup, then prefer the live piece interface over its attribute
// key.
u32 NwDrag::GetTileId(u16 x, u16 z) {
  u32 tileX = x, tileZ = z, result = 0;
  CtCoord point{i32(tileX << kCtMicroCoordinateShift), i32(tileZ << kCtMicroCoordinateShift), 0};
  ScOccupant *occupant = nullptr;
  if (layer->IsInBoundsCell(tileX, tileZ) && layer->GetOccupant(point, &occupant)) {
    NwConnection *piece = nullptr;
    if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&piece))) {
      result = piece->TileId();
      piece->Release();
    } else
      result = occupant->GetAttribData()->Key()->instance;
    occupant->Release();
  }
  return result;
}

// Admit only terrain cells containing no occupant or an occupant of the original byte major type five.
bool NwDrag::EmptyOrTree(CtCoord point) {
  if (!terrain->InCellBounds(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift)))
    return false;
  ScOccupant *occupant = nullptr;
  bool allowed = true;
  if (layer->GetOccupant(point, &occupant)) {
    allowed = occupant->MajorType() == 5;
    occupant->Release();
  }
  return allowed;
}

// Require an in-bounds retained occupant accepted by the primary network predicate, releasing its live publication.
u8 NwDrag::IsUnprotectedRoad(CtCoord point) {
  ScOccupant *occupant = nullptr;
  u8 accepted = 0;
  if (terrain->InCellBounds(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift)) &&
      layer->GetOccupant(point, &occupant)) {
    accepted = primary->Test(occupant);
    occupant->Release();
  }
  return accepted;
}

static CtCoord NwDiagonalPoint(i32 x, i32 z) {
  return {i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0};
}

// Normalize XZ endpoints and discard altitude constraints with the original unspecified-height sentinel.
static void NwDiagonalNormalize(GeBounds *bounds) {
  if (bounds->minimum.x > bounds->maximum.x) {
    i32 value = bounds->minimum.x;
    bounds->minimum.x = bounds->maximum.x;
    bounds->maximum.x = value;
  }
  if (bounds->minimum.z > bounds->maximum.z) {
    i32 value = bounds->minimum.z;
    bounds->minimum.z = bounds->maximum.z;
    bounds->maximum.z = value;
  }
  bounds->minimum.altitude = bounds->maximum.altitude = kCtUnknownCoordinate;
}

static GeBounds NwDiagonalRegion(
    const CtCoord &center, const CtCoord &first, const CtCoord &last, const GeBounds &world) {
  GeBounds area{center, center};
  const CtCoord *points[2] = {&first, &last};
  for (u32 i = 0; i < 2; ++i) {
    const CtCoord *point = points[i];
    if (area.minimum.x > point->x)
      area.minimum.x = point->x;
    if (area.minimum.z > point->z)
      area.minimum.z = point->z;
    if (area.maximum.x < point->x)
      area.maximum.x = point->x;
    if (area.maximum.z < point->z)
      area.maximum.z = point->z;
  }
  area.minimum.x = i32(u32(area.minimum.x) - kCtMicroUnitsPerCell);
  area.minimum.z = i32(u32(area.minimum.z) - kCtMicroUnitsPerCell);
  area.maximum.x = i32(u32(area.maximum.x) + kCtMicroUnitsPerCell);
  area.maximum.z = i32(u32(area.maximum.z) + kCtMicroUnitsPerCell);
  if (area.minimum.x < world.minimum.x)
    area.minimum.x = world.minimum.x;
  if (area.minimum.z < world.minimum.z)
    area.minimum.z = world.minimum.z;
  if (area.maximum.x > world.maximum.x)
    area.maximum.x = world.maximum.x;
  if (area.maximum.z > world.maximum.z)
    area.maximum.z = world.maximum.z;
  area.minimum.altitude = area.maximum.altitude = kCtUnknownCoordinate;
  return area;
}

// Form the XZ union used to repaint both the previous and current preview extents.
static GeBounds NwDiagonalUnion(const GeBounds &first, const GeBounds &last) {
  return {{first.minimum.x < last.minimum.x ? first.minimum.x : last.minimum.x,
              first.minimum.z < last.minimum.z ? first.minimum.z : last.minimum.z, kCtUnknownCoordinate},
      {first.maximum.x > last.maximum.x ? first.maximum.x : last.maximum.x,
          first.maximum.z > last.maximum.z ? first.maximum.z : last.maximum.z, kCtUnknownCoordinate}};
}

NwDiagonalDrag::~NwDiagonalDrag() {
}

// Search five nearby centers in both straight orientations before trying the rotation-specific corner pattern.
// Publish the real selection after updating the old/new cursor union; the original changed-bounds argument is unused.
u8 NwDiagonalDrag::DrawLine(const CtCoord &, const CtCoord &finish, SpSelection **out, GeBounds *) {
  CtCoord center{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, first = center, last = center;
  u8 centerAltitude = 0, firstAltitude = 0, lastAltitude = 0, level = 0;
  u32 centerPiece = 0, firstPiece = 0, lastPiece = 0, firstCost, secondCost;
  bool accepted = false;
  const GeBounds empty{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  GeBounds area = empty, leveled = empty, removed = empty;
  SpSelection *selection = nullptr;
  NwDiagonalNormalize(&changes);
  u32 depth = NwGetCity()->CellCountZ() - 1, width = NwGetCity()->CellCountX();
  GeBounds world{{0, 0, 0},
      {i32((width << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell), i32(depth << kCtMicroCoordinateShift), 0}};
  cursorMap->Clear(false);
  Clear();
  if (!NwGetCurrentView())
    return 0;
  i32 zoom, rotation;
  NwGetCurrentView()->GetZoomAndRotation(&zoom, &rotation);
  for (u32 attempt = 0; attempt < 5 && !accepted; ++attempt) {
    center = finish;
    if (attempt == 0)
      center.z = i32(u32(center.z) + kCtMicroUnitsPerCell);
    else if (attempt == 1)
      center.x = i32(u32(center.x) + kCtMicroUnitsPerCell);
    else if (attempt == 2)
      center.z = i32(u32(center.z) - kCtMicroUnitsPerCell);
    else if (attempt == 3)
      center.x = i32(u32(center.x) - kCtMicroUnitsPerCell);
    for (u32 orientation = 0; orientation < 2 && !accepted; ++orientation) {
      i32 axisX = orientation ? 1 : 0, axisZ = orientation ? 0 : 1;
      u32 straight = orientation ? 15051 : 73, alternate = orientation ? 15098 : 11258;
      i32 x = center.x >> kCtMicroCoordinateShift, z = center.z >> kCtMicroCoordinateShift;
      first = NwDiagonalPoint(x - axisX, z - axisZ);
      last = NwDiagonalPoint(x + axisX, z + axisZ);
      if (!EmptyOrTree(center) || !EmptyOrTree(first) || !EmptyOrTree(last))
        continue;
      i32 normalX = -axisZ, normalZ = -axisX;
      i32 cx = x + normalX, cz = z + normalZ;
      i32 fx = (first.x >> kCtMicroCoordinateShift) + normalX, fz = (first.z >> kCtMicroCoordinateShift) + normalZ;
      i32 lx = (last.x >> kCtMicroCoordinateShift) + normalX, lz = (last.z >> kCtMicroCoordinateShift) + normalZ;
      bool sideReady = false;
      if (terrain->InVertexBounds(u32(cx), u32(cz)) && terrain->InVertexBounds(u32(fx), u32(fz)) &&
          terrain->InVertexBounds(u32(lx), u32(lz))) {
        terrain->GetVertexAltitudeDirt(u32(cx), u32(cz), &centerAltitude);
        terrain->GetVertexAltitudeDirt(u32(fx), u32(fz), &firstAltitude);
        terrain->GetVertexAltitudeDirt(u32(lx), u32(lz), &lastAltitude);
        centerPiece = GetTileId(u16(cx), u16(cz));
        u32 firstSide = GetTileId(u16(fx), u16(fz)), lastSide = GetTileId(u16(lx), u16(lz));
        sideReady = centerPiece == straight && firstSide == straight && lastSide == straight &&
            centerAltitude == firstAltitude && firstAltitude == lastAltitude;
        if (sideReady)
          level = centerAltitude;
      }
      if (!sideReady) {
        normalX = axisZ;
        normalZ = axisX;
        cx = x + normalX;
        cz = z + normalZ;
        fx = (first.x >> kCtMicroCoordinateShift) + normalX;
        fz = (first.z >> kCtMicroCoordinateShift) + normalZ;
        lx = (last.x >> kCtMicroCoordinateShift) + normalX;
        lz = (last.z >> kCtMicroCoordinateShift) + normalZ;
        if (!terrain->InVertexBounds(u32(cx), u32(cz)) || !terrain->InVertexBounds(u32(fx), u32(fz)) ||
            !terrain->InVertexBounds(u32(lx), u32(lz)))
          sideReady = true;
        else {
          terrain->GetVertexAltitudeDirt(u32(cx), u32(cz), &centerAltitude);
          terrain->GetVertexAltitudeDirt(u32(fx), u32(fz), &firstAltitude);
          terrain->GetVertexAltitudeDirt(u32(lx), u32(lz), &lastAltitude);
          centerPiece = GetTileId(u16(cx), u16(cz));
          u32 firstSide = GetTileId(u16(fx), u16(fz)), lastSide = GetTileId(u16(lx), u16(lz));
          sideReady = centerPiece == straight && firstSide == straight && lastSide == straight &&
              centerAltitude == firstAltitude && firstAltitude == lastAltitude;
          if (sideReady)
            level = centerAltitude;
        }
      }
      if (!sideReady)
        continue;
      fx = (first.x >> kCtMicroCoordinateShift) + normalX - axisX;
      fz = (first.z >> kCtMicroCoordinateShift) + normalZ - axisZ;
      lx = (last.x >> kCtMicroCoordinateShift) + normalX + axisX;
      lz = (last.z >> kCtMicroCoordinateShift) + normalZ + axisZ;
      firstPiece = GetTileId(u16(fx), u16(fz));
      lastPiece = GetTileId(u16(lx), u16(lz));
      bool endsReady = (firstPiece == straight || firstPiece == alternate) && lastPiece == straight;
      if (!endsReady) {
        firstPiece = GetTileId(u16(fx), u16(fz));
        lastPiece = GetTileId(u16(lx), u16(lz));
        endsReady = (lastPiece == straight || lastPiece == alternate) && firstPiece == straight;
      }
      if (!endsReady)
        continue;
      CtCoord before =
          NwDiagonalPoint((first.x >> kCtMicroCoordinateShift) - axisX, (first.z >> kCtMicroCoordinateShift) - axisZ);
      CtCoord after =
          NwDiagonalPoint((last.x >> kCtMicroCoordinateShift) + axisX, (last.z >> kCtMicroCoordinateShift) + axisZ);
      if (!IsUnprotectedRoad({before.x, before.z, 0}) && !IsUnprotectedRoad({after.x, after.z, 0}))
        continue;
      area = NwDiagonalRegion(center, first, last, world);
      leveled = empty;
      removed = empty;
      CtCity *currentCity = NwGetCity();
      if (currentCity->DirtBag()->CanLevelTerrain(area, level, &leveled, &secondCost)) {
        GeDemolitionView *demolition =
            static_cast<GeDemolitionView *>(static_cast<CtLayer *>(currentCity->DemolitionLayer()));
        selection = nullptr;
        u8 allowed = demolition->CanDemolishFromDirtChange(&leveled, 1, &firstCost, &removed, &selection, 1);
        ScRelease(&selection);
        if (allowed)
          accepted = true;
      }
    }
  }
  if (!accepted) {
    i32 x = center.x >> kCtMicroCoordinateShift, z = center.z >> kCtMicroCoordinateShift, cx = x, cz = z;
    bool cornerReady = true;
    if (rotation == 0) {
      cx = x - 1;
      cz = z - 1;
    } else if (rotation == 1) {
      cx = x - 1;
      cz = z + 1;
    } else if (rotation == 2) {
      cx = x + 1;
      cz = z + 1;
    } else if (rotation == 3) {
      cx = x + 1;
      cz = z - 1;
    }
    if (rotation >= 0 && rotation <= 3) {
      if (!terrain->InVertexBounds(u32(cx), u32(cz)))
        cornerReady = false;
      else {
        terrain->GetVertexAltitudeDirt(u32(cx), u32(cz), &centerAltitude);
        if (rotation == 0) {
          first = NwDiagonalPoint(x + 1, z);
          last = NwDiagonalPoint(x, z + 1);
          centerPiece = GetTileId(u16(x - 1), u16(z - 1));
          firstPiece = GetTileId(u16(x + 2), u16(z - 1));
          lastPiece = GetTileId(u16(x - 1), u16(z + 2));
        } else if (rotation == 1) {
          first = NwDiagonalPoint(x, z - 1);
          last = NwDiagonalPoint(x + 1, z);
          centerPiece = GetTileId(u16(x - 1), u16(z + 1));
          firstPiece = GetTileId(u16(x - 1), u16(z - 2));
          lastPiece = GetTileId(u16(x + 2), u16(z + 1));
        } else if (rotation == 2) {
          first = NwDiagonalPoint(x - 1, z);
          last = NwDiagonalPoint(x, z - 1);
          centerPiece = GetTileId(u16(x + 1), u16(z + 1));
          firstPiece = GetTileId(u16(x - 2), u16(z + 1));
          lastPiece = GetTileId(u16(x + 1), u16(z - 2));
        } else {
          first = NwDiagonalPoint(x, z + 1);
          last = NwDiagonalPoint(x - 1, z);
          centerPiece = GetTileId(u16(x + 1), u16(z - 1));
          firstPiece = GetTileId(u16(x + 1), u16(z + 2));
          lastPiece = GetTileId(u16(x - 2), u16(z - 1));
        }
      }
    }
    if (cornerReady && EmptyOrTree(center) && EmptyOrTree(first) && EmptyOrTree(last) && centerPiece >= 15111 &&
        centerPiece <= 15114 && (firstPiece == 73 || firstPiece == 15051) && (lastPiece == 73 || lastPiece == 15051)) {
      area = NwDiagonalRegion(center, first, last, world);
      leveled = empty;
      removed = empty;
      if (terrain->CanLevelTerrain(area, centerAltitude, &leveled, &firstCost)) {
        GeDemolitionView *demolition = static_cast<GeDemolitionView *>(static_cast<CtLayer *>(city->DemolitionLayer()));
        if (demolition->CanDemolishFromDirtChange(1, &leveled, 1, &secondCost))
          accepted = true;
      }
    }
  }
  GeBounds affected;
  if (accepted) {
    AddCell(center, 0);
    AddCell(first, 0);
    AddCell(last, 0);
    affected = {first, last};
  } else {
    AddCell(finish, 1);
    affected = {finish, finish};
  }
  NwDiagonalNormalize(&affected);
  GeBounds region = NwDiagonalUnion(changes, affected);
  cursorMap->UpdateRegion(region);
  changes = affected;
  QueryInterface(IID_SpSelection, reinterpret_cast<void **>(out));
  return u8(accepted);
}

// Clamp only out-of-bounds XZ coordinates to the captured city extent, retaining the original altitude word.
static void NwDiagonalClamp(CtCoord *point, const GeBounds &world) {
  if (point->x >= world.minimum.x && point->z >= world.minimum.z && point->x <= world.maximum.x &&
      point->z <= world.maximum.z)
    return;
  if (point->x < world.minimum.x)
    point->x = world.minimum.x;
  if (point->z < world.minimum.z)
    point->z = world.minimum.z;
  if (point->x > world.maximum.x)
    point->x = world.maximum.x;
  if (point->z > world.maximum.z)
    point->z = world.maximum.z;
}

// Require an actual road accepted by the road predicate and absent from the protected-piece table.
// Resolve the piece identifier even after predicate rejection, then release the live retained occupant.
u8 NwManager::IsUnprotectedRoad(CtCoord point) {
  if (!terrain->InCellBounds(u32(point.x >> kCtMicroCoordinateShift), u32(point.z >> kCtMicroCoordinateShift)))
    return 0;
  ScOccupant *occupant = nullptr;
  u8 accepted = 0;
  if (surfaceLayer->GetOccupant(point, &occupant)) {
    bool road = NwPredicates[0]->Test(occupant);
    u32 piece = GetTileId(occupant);
    accepted = road && !IsProtectedPiece(1, piece);
    occupant->Release();
  }
  return accepted;
}

// Price and place the three selected diagonal pieces, preserving separate corner and straight terrain/demolition paths.
// Retile highway then road neighbors and withdraw the accumulated cost; both original geometry outputs are unused.
u8 NwManager::BuildDiagonal(SpSelection *selection, u32 *cost, GeBounds *, CtCoord *) {
  ScOccupantManager *target = surfaceLayer;
  u32 depth = NwGetCity()->CellCountZ() - 1, width = NwGetCity()->CellCountX();
  GeBounds world{{0, 0, 0}, {i32((width - 1) << kCtMicroCoordinateShift), i32(depth << kCtMicroCoordinateShift), 0}};
  DiagonalCost(selection, cost);
  i64 funds = budget->GetTotalFunds();
  if (i64(u64(*cost)) > funds)
    return 0;
  CtCoord points[5];
  for (u32 i = 0; i < 5; ++i)
    points[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 count = 0;
  selection->Reset();
  selection->Next(5, points, &count);
  if (count != 3)
    return 0;
  CtCoord center = points[0], first = points[1], last = points[2];
  if (u32(first.x >> kCtMicroCoordinateShift) < u32(last.x >> kCtMicroCoordinateShift) ||
      u32(first.z >> kCtMicroCoordinateShift) > u32(last.z >> kCtMicroCoordinateShift)) {
    first = points[2];
    last = points[1];
  }
  const GeBounds empty{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  GeBounds area = empty, leveled = empty, removed = empty;
  u32 terrainCost, demolitionCost;
  u8 level = 0, firstAltitude = 0, lastAltitude = 0;
  i32 firstCode = 0, centerCode = 0, lastCode = 0;
  bool corner = ((u32(first.x) ^ center.x) & kCtMicroCellMask || (u32(last.x) ^ center.x) & kCtMicroCellMask) &&
      ((u32(first.z) ^ center.z) & kCtMicroCellMask || (u32(last.z) ^ center.z) & kCtMicroCellMask);
  if (corner) {
    first = points[1];
    last = points[2];
    if ((u32(first.x) ^ center.x) & kCtMicroCellMask) {
      first = points[2];
      last = points[1];
    }
    if (u32(first.z >> kCtMicroCoordinateShift) <= u32(center.z >> kCtMicroCoordinateShift)) {
      if (u32(last.x >> kCtMicroCoordinateShift) <= u32(center.x >> kCtMicroCoordinateShift)) {
        centerCode = kNwDiagonalCornerLowXLowZCenterPiece;
        firstCode = kNwDiagonalCornerLowXLowZFirstPiece;
        lastCode = kNwDiagonalCornerLowXLowZLastPiece;
      } else {
        centerCode = kNwDiagonalCornerHighXLowZCenterPiece;
        firstCode = kNwDiagonalCornerHighXLowZFirstPiece;
        lastCode = kNwDiagonalCornerHighXLowZLastPiece;
      }
    } else if (u32(last.x >> kCtMicroCoordinateShift) <= u32(center.x >> kCtMicroCoordinateShift)) {
      centerCode = kNwDiagonalCornerLowXHighZCenterPiece;
      firstCode = kNwDiagonalCornerLowXHighZFirstPiece;
      lastCode = kNwDiagonalCornerLowXHighZLastPiece;
    } else {
      centerCode = kNwDiagonalCornerHighXHighZCenterPiece;
      firstCode = kNwDiagonalCornerHighXHighZFirstPiece;
      lastCode = kNwDiagonalCornerHighXHighZLastPiece;
    }
    i32 zoom = 0, rotation = 0;
    if (NwGetCurrentView())
      NwGetCurrentView()->GetZoomAndRotation(&zoom, &rotation);
    i32 x = center.x >> kCtMicroCoordinateShift, z = center.z >> kCtMicroCoordinateShift;
    if (rotation == 0)
      terrain->GetVertexAltitudeDirt(u32(x - 1), u32(z - 1), &level);
    else if (rotation == 1)
      terrain->GetVertexAltitudeDirt(u32(x - 1), u32(z + 1), &level);
    else if (rotation == 2)
      terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z + 1), &level);
    else if (rotation == 3)
      terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z - 1), &level);
    area = NwDiagonalRegion(center, first, last, world);
    leveled = empty;
    removed = empty;
    if (terrain->LevelTerrain(area, level, &leveled, &terrainCost, true)) {
      GeDemolitionView *demolition = static_cast<GeDemolitionView *>(static_cast<CtLayer *>(city->DemolitionLayer()));
      if (!demolition->DemolishFromDirtChange(&leveled, 1, 0, &demolitionCost, &removed, 1))
        return 0;
      *cost += terrainCost + demolitionCost;
    }
  } else {
    i32 dx = (last.x >> kCtMicroCoordinateShift) - (center.x >> kCtMicroCoordinateShift),
        dz = (last.z >> kCtMicroCoordinateShift) - (center.z >> kCtMicroCoordinateShift);
    i32 normalX = -dz, normalZ = -dx;
    CtCoord centerProbe = NwDiagonalPoint(
        (center.x >> kCtMicroCoordinateShift) + i16(normalX), (center.z >> kCtMicroCoordinateShift) + i16(normalZ));
    CtCoord firstProbe = NwDiagonalPoint(
        (first.x >> kCtMicroCoordinateShift) + i16(normalX), (first.z >> kCtMicroCoordinateShift) + i16(normalZ));
    CtCoord lastProbe = NwDiagonalPoint(
        (last.x >> kCtMicroCoordinateShift) + i16(normalX), (last.z >> kCtMicroCoordinateShift) + i16(normalZ));
    centerProbe.altitude = i32(u32(center.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
    firstProbe.altitude = i32(u32(first.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
    lastProbe.altitude = i32(u32(last.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
    NwDiagonalClamp(&centerProbe, world);
    NwDiagonalClamp(&firstProbe, world);
    NwDiagonalClamp(&lastProbe, world);
    u32 centerPiece =
        GetTileId(u16(centerProbe.x >> kCtMicroCoordinateShift), u16(centerProbe.z >> kCtMicroCoordinateShift));
    u32 firstPiece =
        GetTileId(u16(firstProbe.x >> kCtMicroCoordinateShift), u16(firstProbe.z >> kCtMicroCoordinateShift));
    u32 lastPiece = GetTileId(u16(lastProbe.x >> kCtMicroCoordinateShift), u16(lastProbe.z >> kCtMicroCoordinateShift));
    terrain->GetVertexAltitudeDirt(
        u32(centerProbe.x >> kCtMicroCoordinateShift), u32(centerProbe.z >> kCtMicroCoordinateShift), &level);
    terrain->GetVertexAltitudeDirt(
        u32(firstProbe.x >> kCtMicroCoordinateShift), u32(firstProbe.z >> kCtMicroCoordinateShift), &firstAltitude);
    terrain->GetVertexAltitudeDirt(
        u32(lastProbe.x >> kCtMicroCoordinateShift), u32(lastProbe.z >> kCtMicroCoordinateShift), &lastAltitude);
    u32 straight = u16(dx) ? 15051 : 73, alternate = u16(dx) ? 15098 : 11258;
    bool negative = centerPiece == straight && firstPiece == straight && lastPiece == straight &&
        level == firstAltitude && firstAltitude == lastAltitude;
    bool positive = false;
    if (!negative) {
      normalX = dz;
      normalZ = dx;
      centerProbe = NwDiagonalPoint(
          (center.x >> kCtMicroCoordinateShift) + i16(normalX), (center.z >> kCtMicroCoordinateShift) + i16(normalZ));
      firstProbe = NwDiagonalPoint(
          (first.x >> kCtMicroCoordinateShift) + i16(normalX), (first.z >> kCtMicroCoordinateShift) + i16(normalZ));
      lastProbe = NwDiagonalPoint(
          (last.x >> kCtMicroCoordinateShift) + i16(normalX), (last.z >> kCtMicroCoordinateShift) + i16(normalZ));
      centerProbe.altitude = i32(u32(center.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      firstProbe.altitude = i32(u32(first.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      lastProbe.altitude = i32(u32(last.altitude >> kCtMicroCoordinateShift) << kCtMicroCoordinateShift);
      NwDiagonalClamp(&centerProbe, world);
      NwDiagonalClamp(&firstProbe, world);
      NwDiagonalClamp(&lastProbe, world);
      centerPiece =
          GetTileId(u16(centerProbe.x >> kCtMicroCoordinateShift), u16(centerProbe.z >> kCtMicroCoordinateShift));
      firstPiece =
          GetTileId(u16(firstProbe.x >> kCtMicroCoordinateShift), u16(firstProbe.z >> kCtMicroCoordinateShift));
      lastPiece = GetTileId(u16(lastProbe.x >> kCtMicroCoordinateShift), u16(lastProbe.z >> kCtMicroCoordinateShift));
      terrain->GetVertexAltitudeDirt(
          u32(centerProbe.x >> kCtMicroCoordinateShift), u32(centerProbe.z >> kCtMicroCoordinateShift), &level);
      terrain->GetVertexAltitudeDirt(
          u32(firstProbe.x >> kCtMicroCoordinateShift), u32(firstProbe.z >> kCtMicroCoordinateShift), &firstAltitude);
      terrain->GetVertexAltitudeDirt(
          u32(lastProbe.x >> kCtMicroCoordinateShift), u32(lastProbe.z >> kCtMicroCoordinateShift), &lastAltitude);
      if (centerPiece != straight || firstPiece != straight || lastPiece != straight || level != firstAltitude ||
          firstAltitude != lastAltitude)
        return 0;
      positive = true;
    }
    i32 fx = first.x >> kCtMicroCoordinateShift, fz = first.z >> kCtMicroCoordinateShift,
        lx = last.x >> kCtMicroCoordinateShift, lz = last.z >> kCtMicroCoordinateShift;
    CtCoord before = NwDiagonalPoint(fx - i16(dx), fz - i16(dz));
    CtCoord after = NwDiagonalPoint(lx + i16(dx), lz + i16(dz));
    u32 beforePiece = GetTileId(u16(u32(fx) + normalX - dx), u16(u32(fz) + normalZ - dz));
    u32 afterPiece = GetTileId(u16(u32(lx) + dx + normalX), u16(u32(lz) + dz + normalZ));
    bool beforeAccepted = false, afterAccepted = false;
    if ((beforePiece == straight || beforePiece == alternate) && afterPiece == straight &&
        IsUnprotectedRoad({before.x, before.z, 0}))
      beforeAccepted = true;
    else {
      beforePiece = GetTileId(u16(u32(fx) + normalX - dx), u16(u32(fz) + normalZ - dz));
      afterPiece = GetTileId(u16(u32(lx) + dx + normalX), u16(u32(lz) + dz + normalZ));
      if ((afterPiece == straight || afterPiece == alternate) && beforePiece == straight &&
          IsUnprotectedRoad({after.x, after.z, 0}))
        afterAccepted = true;
      else
        return 0;
    }
    if (!u16(dx)) {
      if (negative) {
        if (beforeAccepted) {
          firstCode = kNwDiagonalZeroDxNegativeBeforeFirstPiece;
          centerCode = kNwDiagonalZeroDxNegativeBeforeCenterPiece;
          lastCode = kNwDiagonalZeroDxNegativeBeforeLastPiece;
        }
        if (afterAccepted) {
          firstCode = kNwDiagonalZeroDxNegativeAfterFirstPiece;
          centerCode = kNwDiagonalZeroDxNegativeAfterCenterPiece;
          lastCode = kNwDiagonalZeroDxNegativeAfterLastPiece;
        }
      }
      if (positive) {
        if (afterAccepted) {
          firstCode = kNwDiagonalZeroDxPositiveAfterFirstPiece;
          centerCode = kNwDiagonalZeroDxPositiveAfterCenterPiece;
          lastCode = kNwDiagonalZeroDxPositiveAfterLastPiece;
        }
        if (beforeAccepted) {
          firstCode = kNwDiagonalZeroDxPositiveBeforeFirstPiece;
          centerCode = kNwDiagonalZeroDxPositiveBeforeCenterPiece;
          lastCode = kNwDiagonalZeroDxPositiveBeforeLastPiece;
        }
      }
    }
    if (!u16(dz)) {
      if (positive) {
        if (beforeAccepted) {
          firstCode = kNwDiagonalZeroDzPositiveBeforeFirstPiece;
          centerCode = kNwDiagonalZeroDzPositiveBeforeCenterPiece;
          lastCode = kNwDiagonalZeroDzPositiveBeforeLastPiece;
        }
        if (afterAccepted) {
          firstCode = kNwDiagonalZeroDzPositiveAfterFirstPiece;
          centerCode = kNwDiagonalZeroDzPositiveAfterCenterPiece;
          lastCode = kNwDiagonalZeroDzPositiveAfterLastPiece;
        }
      }
      if (negative) {
        if (beforeAccepted) {
          firstCode = kNwDiagonalZeroDzNegativeBeforeFirstPiece;
          centerCode = kNwDiagonalZeroDzNegativeBeforeCenterPiece;
          lastCode = kNwDiagonalZeroDzNegativeBeforeLastPiece;
        }
        if (afterAccepted) {
          firstCode = kNwDiagonalZeroDzNegativeAfterFirstPiece;
          centerCode = kNwDiagonalZeroDzNegativeAfterCenterPiece;
          lastCode = kNwDiagonalZeroDzNegativeAfterLastPiece;
        }
      }
    }
    area = NwDiagonalRegion(center, first, last, world);
    leveled = empty;
    removed = empty;
    if (!terrain->LevelTerrain(area, level, &leveled, &terrainCost, true))
      return 0;
    GeDemolitionView *demolition = static_cast<GeDemolitionView *>(static_cast<CtLayer *>(city->DemolitionLayer()));
    if (!demolition->DemolishFromDirtChange(&leveled, 1, 0, &demolitionCost, &removed, 1))
      return 0;
    *cost += terrainCost + demolitionCost;
    if (!beforeAccepted && !afterAccepted)
      return 0;
  }
  PlaceSpan(target, first, firstCode, -1, 8, 0);
  PlaceSpan(target, center, centerCode, -1, 8, 0);
  PlaceSpan(target, last, lastCode, -1, 8, 0);
  NwPositionList pending;
  GeBounds selected = {points[0], points[0]}, retiled = empty;
  if (selected.minimum.x > points[1].x)
    selected.minimum.x = points[1].x;
  if (selected.minimum.z > points[1].z)
    selected.minimum.z = points[1].z;
  if (selected.maximum.x < points[1].x)
    selected.maximum.x = points[1].x;
  if (selected.maximum.z < points[1].z)
    selected.maximum.z = points[1].z;
  if (selected.minimum.x > points[2].x)
    selected.minimum.x = points[2].x;
  if (selected.minimum.z > points[2].z)
    selected.minimum.z = points[2].z;
  if (selected.maximum.x < points[2].x)
    selected.maximum.x = points[2].x;
  if (selected.maximum.z < points[2].z)
    selected.maximum.z = points[2].z;
  selected.minimum.altitude = selected.maximum.altitude = kCtUnknownCoordinate;
  CollectRegion(target, NwPredicates[1], 4, selected, &pending);
  if (pending.head->next != pending.head)
    ApplyTiling(pending, 4, &retiled);
  pending.Clear();
  CollectRegion(target, NwPredicates[0], 1, selected, &pending);
  if (pending.head->next != pending.head)
    ApplyTiling(pending, 1, &retiled);
  pending.Clear();
  budget->WithdrawFunds(*cost);
  return 1;
}

#if defined(_WIN32)
#include <float.h>
#endif

static const u32 kNwNotificationTypes[8] = {kOcMsg_SurfaceInserted, kOcMsg_SurfaceRemoved, kOcMsg_PipeInserted,
    kOcMsg_PipeRemoved, kOcMsg_SubwayInserted, kOcMsg_SubwayRemoved, kNwMsg_RefreshNetwork, kNwMsg_SetTransitDamage};

// Initialize the six network counts, retained fields, damage fraction and original INT_MAX coordinate bounds.
// Terrain, zone and neighbors remain unwritten until city initialization; the four established bases own no fields.
NwManager::NwManager()
    : roadCount(0), highwayCount(0), railCount(0), subwayCount(0), pipeCount(0), powerCount(0), references(0),
      city(nullptr), budget(nullptr), surfaceLayer(nullptr), undergroundOne(nullptr), undergroundTwo(nullptr),
      damageFraction(0), bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
                             {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      debugRules(0) {
}

// Publish the actual primary, object-factory, occupant-factory or query-string subobject before the live virtual retain.
// Unsupported identities clear the caller's output and leave the manager's reference count unchanged.
bool NwManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_NwManager, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_OcFactory, OcFactory);
  HANDLE_QUERY_INTERFACE(IID_ScQueryInfoString, ScQueryInfoString);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Allocate the original manager with its four real interface bases and return the unretained primary identity.
// Construction leaves city-service acquisition to Init, preserving its original zero reference count.
ScUnknown *NwCreateManager() {
  return static_cast<CtLayer *>(new NwManager());
}

bool NwManager::CreateMapSprite(u32, u32, void **) {
  return false;
}

u32 NwManager::GetMapSpriteCount() {
  return 0;
}

bool NwManager::GetSpriteIdFromIndex(u32, u32 *) {
  return false;
}

u32 NwManager::GetInfoStringCount(u32) {
  return 0;
}

u8 NwManager::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

// Retain the manager before the real module director, then return the live manager count after callbacks.
u32 NwManager::AddRef() {
  ++references;
  NwGetDirector()->AddRef();
  return references;
}

// Decrement before releasing the module and reread the actual manager count afterward.
// A zero live count invokes the real virtual destructor; return the captured result without reading deleted storage.
u32 NwManager::Release() {
  --references;
  NwGetDirector()->RemoveRef();
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

NwManager::~NwManager() {
}

bool NwManager::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

bool NwManager::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool NwManager::SimulationEnd() {
  return true;
}

// Register the eight original notifications, then post occupants from the city's three live managers in order.
// Callback results are ignored; reload the actual city member before each layer query and return true.
bool NwManager::SimulationBegin() {
  ScMessageServer *messages = NwGetMessageServer();
  for (u32 i = 0; i < 8; ++i)
    messages->AddNotification(this, kNwNotificationTypes[i]);
  city->SurfaceOccupantManager()->PostOccupantAll(this);
  city->UndergroundOccupantManagerLevelOne()->PostOccupantAll(this);
  city->UndergroundOccupantManagerLevelTwo()->PostOccupantAll(this);
  return true;
}

u32 NwManager::GetLayerType() {
  return kNwManagerLayerType;
}

bool NwManager::DebugClassTag(ScStringView *) {
  return false;
}

bool NwManager::DebugTypeTag(ScStringView *) {
  return false;
}

// Remove all eight original notifications before releasing city services in their original order.
// Read each live field at its turn and clear it after Release so callback mutations retain their source effects.
bool NwManager::Shutdown() {
  ScMessageServer *messages = NwGetMessageServer();
  for (u32 i = 0; i < 8; ++i)
    messages->RemoveNotification(this, kNwNotificationTypes[i]);
  ScRelease(&surfaceLayer);
  ScRelease(&undergroundOne);
  ScRelease(&undergroundTwo);
  ScRelease(&terrain);
  ScRelease(&zone);
  ScRelease(&budget);
  ScRelease(&neighbors);
  ScRelease(&city);
  return true;
}

// The exact factor 5368709/2^29 handles single/extended precision; double precision retains its intermediate rounding.
static f32 NwDamageFraction(u32 percent) {
#if defined(_WIN32)
  u32 control;
  _controlfp_s(&control, 0, 0);
  if ((control & _MCW_PC) == _PC_53)
    return f32(f64(percent) * f64(0.01f));
#endif
  return f32(u64(percent) * kNwDamagePercentageQ29Reciprocal) * 0x1p-29f;
}
