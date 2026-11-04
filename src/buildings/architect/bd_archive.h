#ifndef SC3_BUILDINGS_ARCHITECT_BD_ARCHIVE_H
#define SC3_BUILDINGS_ARCHITECT_BD_ARCHIVE_H

#include <vector>

#include "buildings/architect/codec.h"
#include "common/sc3_qfs.h"
#include "resources/indexed_file.h"
#include "sprites/module.h"

// Stored building-archive record identities; each type retains its separate group and instance words.
enum BdArchiveRecordKey : u32 {
  kBdBuildingArchiveGroup = 0xc3f8d4a9,
  kBdGridRecordType = 0xe3f87b0c,
  kBdGridRecordInstance = 0xa3f8d615,
  kBdDocumentRecordType = 0xe3f87b0f,
  kBdDocumentRecordInstance = 0x63fbe1cc,
  kBdPreviewRecordType = 0xe3f87b0e,
  kBdPreviewRecordInstance = 0xe3f8d655,
  kBdAssetRecordType = 0xe3f87b0d,
  kBdObjectDefinitionRecordType = 0xfa662d17,
};

class BdCatalog;
class BdAsset;
class BdDefinition;
class BdDocument;
class BdGrid;
class BdExportFrames;
struct BdArchiveKeyContext;
using BdArchiveKeyVisitor = void (*)(RsKey *key, BdArchiveKeyContext *context);

// The directory writers consume twenty 36-byte frame records following a 48-byte original header.
// The aggregate's source name and unconsumed fields remain unresolved; only the listed fields are read here.
struct BdFrameDirectoryEntry {
  u8 field_00;
  i8 field_01;
  i16 field_02, field_04;
  u8 field_06[10];
  u32 instance, group;
  u8 field_18[12];
};

struct BdFrameDirectory {
  u8 header[48];
  BdFrameDirectoryEntry entries[20];
};
static_assert(sizeof(BdFrameDirectoryEntry) == 36 && offsetof(BdFrameDirectoryEntry, instance) == 16);

// SIMBABLD's local copy of the image-archive base uses this DLL's own graphics and COM services.
// Private unused decoder and cache fields remain unrecovered.
class BdImageArchive : public SpImageSource, public RsIndexedFile {
public:
  BdImageArchive();
  ~BdImageArchive() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 identifier, const ScStringView *name, bool resolved) override;
  bool Shutdown() override;
  // Local graphics/resource image loading preserves the original one-byte result.
  u8 ReadImage(GxBuffer **out, const RsKey *key) override;
  u8 ReadMetadata(SpImage *out, const RsKey *key) override;
  u8 DecodePixels(GxBuffer *target, const u8 *source, u32 size, u32 flags, i32 format);
  u8 DecodePlainBitmap(GxBuffer *target, const u8 *source, u32 size);
  u8 DecodeTransparentRows(GxBuffer *target, const u8 *source, u32 size, i32 format);
  u8 DecodeTransparentColumns(GxBuffer *target, const u8 *source, u32 size, i32 format);
  u8 DecodePaletteRows(GxBuffer *target, const u8 *source, u32 size, i32 format);
  u8 DecodePaletteColumns(GxBuffer *target, const u8 *source, u32 size, i32 format);

  u32 field_0ec, field_0f0;
  u16 color_key;
  i16 target_format;
  u32 field_0f8;
  u8 field_0fc, field_0fd;
  ScQfs codec;
  u32 field_100;
};

// The architect archive extends its local image archive with the open flag and embedded codec.
// Remaining original virtual operations remain explicit dependencies.
class BdArchive : public BdImageArchive {
public:
  BdArchive();
  ~BdArchive() override;
  virtual u8 OpenArchive(const ScStringView *name);
  virtual u8 CreateArchive(const ScStringView *name);
  virtual u8 CloseArchive();
  virtual u8 WriteSprite(const RsKey *key, GxBuffer *image, const BdRect *bounds);
  virtual u8 WriteDirectory(const RsKey *key, const BdFrameDirectory *directory);
  virtual u8 WriteDirectoryWithTerminator(const RsKey *key, const BdFrameDirectory *directory);
  // The installed reader appends to two original STL vectors of three-word resource keys.
  virtual u8 ReadDirectoryKeys(const RsKey *key, std::vector<RsKey> *rectangles, std::vector<RsKey> *images);
  virtual u8 WriteFrames(u32 group, u32 instance, const BdExportFrames *frames);
  // Original intermediate slot 16; preserves the observed byte result.
  virtual u8 WriteImage(const RsKey *key, GxBuffer *image);
  virtual u8 ReadStoredImage(const RsKey *key, GxBuffer **out);

  u8 archive_open;
  BdQfsCodec archive_codec;
};

// The building archive adds its borrowed catalog after the intermediate archive state.
// Remaining resource serialization methods remain explicit dependencies.
class BdIxf : public BdArchive {
public:
  BdIxf();
  ~BdIxf() override;
  // The private setter's original source return declaration remains unresolved; callers discard its effect result.
  void SetCatalog(BdCatalog *value);
  virtual u8 ReadGrid(const RsKey *key, BdGrid **out);
  virtual u8 WriteGrid(const RsKey *key, BdGrid *grid);
  virtual u8 ReadPreviewImage(const RsKey *key, GxBuffer **out);
  virtual u8 WritePreviewImage(const RsKey *key, GxBuffer *image);
  // The unused third x86 argument is a DWORD; its original source type remains unresolved.
  virtual u8 ReadAsset(const RsKey *key, BdAsset **out, u32 unused);
  virtual u8 WriteAsset(const RsKey *key, BdAsset *value);
  virtual u8 ReadDocument(const RsKey *key, BdDocument *out);
  virtual u8 WriteDocument(const RsKey *key, const BdDocument *value);
  virtual u8 ReadDefinition(const RsKey *key, BdDefinition **out);
  virtual u8 WriteDefinition(const RsKey *key, const BdDefinition *value);
  virtual u8 WriteGridInfo(u32 group, u32 instance, const BdGrid *grid);
  u8 ReadGridInfoKey(const RsKey *key, RsKey *out);
  virtual u8 EnumGrids(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumPreviews(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumAssets(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumDocuments(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumStoredImages(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumGridInfo(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);
  virtual u8 EnumDefinitions(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context);

  BdCatalog *catalog;
};

// Native legacy-text boundary preflights, separate from the original methods' intentionally ignored I/O results.
bool BdCanEncodeDocumentText(const BdGrid *grid);
bool BdCanEncodeGridText(const BdGrid *grid, BdCatalog *catalog);

u8 BdCopyArchiveRecord(
    BdArchive *source, const RsKey *source_key, BdArchive *destination, const RsKey *destination_key);

#endif
