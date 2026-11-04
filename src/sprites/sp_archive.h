#ifndef SC3_SPRITES_SP_ARCHIVE_H
#define SC3_SPRITES_SP_ARCHIVE_H

#include "common/sc3.h"
#include "common/sc3_qfs.h"
#include "resources/indexed_file.h"
#include "sprites/module.h"

class GxBuffer;

// Actual DAT image-source and indexed-file identities share the indexed segment's reference count.
// Native inheritance expresses their adjustments; private unused decoder/cache fields remain unrecovered.
class SpArchive : public SpImageSource, public RsIndexedFile {
public:
  SpArchive();
  ~SpArchive() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 id, const ScStringView *name, bool resolved) override;
  bool Shutdown() override;
  u8 ReadImage(GxBuffer **out, const RsKey *key) override;
  u8 ReadMetadata(SpImage *out, const RsKey *key) override;
  u8 DecodePixels(GxBuffer *target, const u8 *source, u32 payload_size, u32 flags, i32 source_format);
  u8 DecodePlainBitmap(GxBuffer *target, const u8 *source, u32 payload_size);
  u8 DecodeTransparentRows(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
  u8 DecodeTransparentColumns(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
  u8 DecodePaletteRows(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
  u8 DecodePaletteColumns(GxBuffer *target, const u8 *source, u32 payload_size, i32 source_format);
  u16 color_key;     // Original archive WORD +244, read live by transparent packets.
  i16 target_format; // Original signed WORD +246, captured from the live graphics format at Init.
  ScQfs codec;       // Original embedded receiver +254; pixel methods preserve this ownership boundary.
};

#endif
