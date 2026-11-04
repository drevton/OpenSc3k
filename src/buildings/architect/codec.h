#ifndef SC3_BUILDINGS_ARCHITECT_CODEC_H
#define SC3_BUILDINGS_ARCHITECT_CODEC_H

#include "buildings/architect/bd_geometry.h"
#include "buildings/architect/image.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// IIDC3023A9D exposes the original mixed-convention codec wrapper. Its PC object is8 bytes, with refs at+4.
class BdCodec : public ScUnknown {
public:
  BdCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual i32 GetValue0C();
  virtual i32 GetValue10();
  virtual BdCodecDescriptor *BD_STDCALL Describe();
  virtual i32 BD_STDCALL CanRead(BdCodecFile *file);
  virtual i32 BD_STDCALL OpenReader(BdCodecState **out, BdCodecFile *file, const char *path);
  virtual BdCodecInfo *BD_STDCALL InfoAt(BdCodecState *state, i32 index);
  virtual i32 BD_STDCALL ReadPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
  virtual i32 BD_STDCALL CloseReader(BdCodecState *state);
  virtual i32 BD_STDCALL OpenWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
  virtual i32 BD_STDCALL WritePixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
  virtual i32 BD_STDCALL CloseWriter(BdCodecState *state);
  virtual i32 BD_STDCALL SelectForFile(const char *path, BdCodecFile *file);
  virtual BdCodecFile *BD_STDCALL OpenFile(const char *path, char *resolved);
  virtual i32 BD_STDCALL SetSelected(u32 index);
  virtual u32 BD_STDCALL GetSelected();
  virtual u32 BD_STDCALL SelectExtension(const char *extension, i32 *slot);
  virtual i32 BD_STDCALL GetCodecCount();
  // Disposal uses the recovered effects; its private return declaration remains unresolved.
  virtual void BD_STDCALL DestroyPixels(BdCodecPixels *pixels);
  virtual BdCodecPixels *BD_STDCALL Decode(const char *path, i32 index);
  virtual BdCodecDescriptor *BD_STDCALL DescribeBmp();
  virtual i32 BD_STDCALL ProbeBmp(BdCodecFile *file);
  virtual i32 BD_STDCALL OpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *path);
  virtual BdCodecInfo *BD_STDCALL BmpInfoAt(BdCodecState *state, i32 index);
  virtual i32 BD_STDCALL ReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
  virtual i32 BD_STDCALL CloseBmpReader(BdCodecState *state);
  virtual i32 BD_STDCALL OpenBmpWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
  virtual i32 BD_STDCALL WriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
  virtual i32 BD_STDCALL CloseBmpWriter(BdCodecState *state);
  virtual BdCodecDescriptor *BD_STDCALL DescribeTga();
  virtual i32 BD_STDCALL ProbeTga(BdCodecFile *file);
  virtual i32 BD_STDCALL OpenTgaReader(BdCodecState **out, BdCodecFile *file, const char *path);
  virtual BdCodecInfo *BD_STDCALL TgaInfoAt(BdCodecState *state, i32 index);
  virtual i32 BD_STDCALL ReadTgaPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
  virtual i32 BD_STDCALL CloseTgaReader(BdCodecState *state);
  virtual i32 BD_STDCALL OpenTgaWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
  virtual i32 BD_STDCALL WriteTgaPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
  virtual i32 BD_STDCALL CloseTgaWriter(BdCodecState *state);
  virtual ~BdCodec();

  u32 references;
};

// IIDC3023A9C uses ordinary member calls, with its own reference count at PC+4.
// These are the established operations through original slot30. Two trailing nine-slot groups remain unresolved;
// this native interface subset is not a foreign vtable overlay or a claim of complete interface recovery.
class BdLegacyCodec : public ScUnknown {
public:
  BdLegacyCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual i32 GetValue0C();
  virtual i32 GetValue10();
  virtual BdCodecDescriptor *Describe();
  virtual i32 CanRead(BdCodecFile *file);
  virtual i32 OpenReader(BdCodecState **out, BdCodecFile *file, const char *path);
  virtual BdCodecInfo *InfoAt(BdCodecState *state, i32 index);
  virtual i32 ReadPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
  virtual i32 CloseReader(BdCodecState *state);
  virtual i32 OpenWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
  virtual i32 WritePixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
  virtual i32 CloseWriter(BdCodecState *state);
  virtual i32 SelectForFile(const char *path, BdCodecFile *file);
  virtual BdCodecFile *OpenFile(const char *path, char *resolved);
  virtual i32 SetSelected(u32 index);
  virtual u32 GetSelected();
  virtual u32 SelectExtension(const char *extension, i32 *slot);
  virtual i32 GetCodecCount();
  // Disposal uses the recovered effects; its private return declaration remains unresolved.
  virtual void DestroyPixels(BdCodecPixels *pixels);
  virtual BdCodecPixels *Decode(const char *path, i32 index);
  virtual BdCodecDescriptor *DescribeBmp();
  virtual i32 ProbeBmp(BdCodecFile *file);
  virtual i32 OpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *path);
  virtual BdCodecInfo *BmpInfoAt(BdCodecState *state, i32 index);
  virtual i32 ReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
  virtual i32 CloseBmpReader(BdCodecState *state);
  virtual i32 OpenBmpWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C);
  virtual i32 WriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
  virtual i32 CloseBmpWriter(BdCodecState *state);
  virtual ~BdLegacyCodec();

  u32 references;
};

u32 BD_STDCALL BdQfsEncodeBody(const u8 *source, i32 length, u8 *destination, i32 window, i32 fast);
u32 BD_STDCALL BdQfsEncode(const u8 *source, i32 length, u8 *destination, const i32 *option);
// Native input boundary for the original unbounded QFS decoder; this is not a game codec method.
bool BdValidateQfsInput(const u8 *data, u32 size, u32 *decoded_size);

// Original eight-byte reference-counted codec embedded in the architect archive's intermediate base.
class BdQfsCodec : public ScUnknown {
public:
  BdQfsCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Compress(const u8 *source, i32 length, u8 *destination, u32 *written);
  virtual bool Decompress(const u8 *source, u32 length, u8 *destination, u32 *written);
  virtual u32 CompressCapacity(u32 length);
  virtual u32 CompressedSize(const u8 *source);
  virtual u32 DecompressedSize(const u8 *source);
  // The installed callers discard the incidental private result of the version-copy operation.
  virtual void VersionEffects(ScStringView *out);
  virtual ~BdQfsCodec();

  u32 references;
};

#endif
