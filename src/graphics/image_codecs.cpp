#define _CRT_SECURE_NO_WARNINGS
#include "graphics/image_codecs.h"

#include <assert.h>
#include <csetjmp>
#include <cstddef>
#include <cstring>

#include "common/ids.h"
#include "common/sc3.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/path.h"
#include "graphics/sc3_graphics.h"

static u32 GxGimexSelection;
ScDirector *GxGetGimexDirector();
i32 GX_GIMEX_CALL GxGimexProbeJpeg(GxGimexFile *file);
GxGimexInfo *GX_GIMEX_CALL GxGimexJpegInfoAt(GxGimexState *state, i32 index);
i32 GX_GIMEX_CALL GxGimexReadJpeg(GxGimexState *state, GxGimexInfo *info, u8 *out, i32 pitch);
i32 GX_GIMEX_CALL GxGimexWriteJpeg(GxGimexState *state, GxGimexInfo *info, u8 *source, i32 pitch);
i32 GX_GIMEX_CALL GxGimexOpenJpegReader(GxGimexState **out, GxGimexFile *file, const char *path);
i32 GX_GIMEX_CALL GxGimexCreateWriter(GxGimexState **out, GxGimexFile *file, const char *path, i32 field);
i32 GX_GIMEX_CALL GxGimexCloseState(GxGimexState *state);

GxGimexCodec::GxGimexCodec() : references(0) {
}

// Construct the registered codec object with its initial zero reference count.
ScUnknown *GxCreateGimexCodec() {
  return new GxGimexCodec();
}

GxGimexCodec::~GxGimexCodec() {
}

bool GxGimexCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxGimexCodec);
  HANDLE_QUERY_INTERFACE(IID_GxGimexCodec, GxGimexCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the actual GIMEX module director.
u32 GxGimexCodec::AddRef() {
  ++references;
  GxGetGimexDirector()->AddRef();
  return references;
}

// Release one module reference for a positive count and delete the codec when its count reaches zero.
u32 GxGimexCodec::Release() {
  if (references) {
    GxGetGimexDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

i32 GxGimexCodec::LibraryMajorVersion() {
  return 2;
}

i32 GxGimexCodec::LibraryMinorVersion() {
  return 21;
}

// Construct the descriptor for the currently selected linked format.
GxGimexDescriptor *GxGimexCodec::AboutGimex() {
  return GxGimexDescribe();
}

i32 GxGimexCodec::IsGimex(void *file) {
  return GxGimexCanRead(static_cast<GxGimexFile *>(file));
}

i32 GxGimexCodec::OpenGimex(GxGimexState **out, void *file, const char *path) {
  return GxGimexOpenReader(out, static_cast<GxGimexFile *>(file), path);
}

// Read the requested image metadata from the selected reader.
GxGimexInfo *GxGimexCodec::InfoGimex(GxGimexState *state, i32 index) {
  return GxGimexInfoAt(state, index);
}

i32 GxGimexCodec::ReadGimex(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch) {
  return GxGimexReadPixels(state, info, reinterpret_cast<u8 *>(out), pitch);
}

i32 GxGimexCodec::CloseGimex(GxGimexState *state) {
  return GxGimexCloseReader(state);
}

i32 GxGimexCodec::OpenGimexWriter(GxGimexState **out, void *file, const char *path, i32 field) {
  return GxGimexOpenWriter(out, static_cast<GxGimexFile *>(file), path, field);
}

i32 GxGimexCodec::WriteGimex(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch) {
  return GxGimexWritePixels(state, info, reinterpret_cast<u8 *>(source), pitch);
}

i32 GxGimexCodec::CloseGimexWriter(GxGimexState *state) {
  return GxGimexCloseWriter(state);
}

i32 GxGimexCodec::OpenGimexStream(const char *path, void *file) {
  return GxGimexSelectForFile(path, static_cast<GxGimexFile *>(file));
}

// Open the requested image and publish the optional resolved name.
void *GxGimexCodec::OpenGimexFile(const char *path, char *resolved) {
  return GxGimexOpenFile(path, resolved);
}

i32 GxGimexCodec::SetGimex(i32 index) {
  return GxGimexSelect(index);
}

u32 GxGimexCodec::GetGimex() {
  return GxGimexSelected();
}

u32 GxGimexCodec::FindGimex(char *extension, i32 *slot) {
  return GxGimexSelectExtension(extension, slot);
}

i32 GxGimexCodec::GetNumGimex() {
  return GxGimexCodecCount();
}

void GxGimexCodec::FreeBitmap(GxGimexPixels *pixels) {
  GxGimexDestroyPixels(pixels);
}

// Decode an image using the original selected-format dispatch and restore its prior selection.
GxGimexPixels *GxGimexCodec::EzLoadFrame(const char *path, i32 index) {
  return GxGimexDecode(path, index);
}

// Keep the original virtual slot unavailable after removing non-JPEG image-file support.
GxGimexDescriptor *GxGimexCodec::AboutBmp() {
  return nullptr;
}

i32 GxGimexCodec::IsBmp(void *) {
  return 0;
}

i32 GxGimexCodec::OpenBmp(GxGimexState **, void *, char *) {
  return 0;
}

// Keep the original virtual slot unavailable after removing non-JPEG image-file support.
GxGimexInfo *GxGimexCodec::InfoBmp(GxGimexState *, i32) {
  return nullptr;
}

i32 GxGimexCodec::ReadBmp(GxGimexState *, GxGimexInfo *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::CloseBmp(GxGimexState *) {
  return 0;
}

i32 GxGimexCodec::OpenBmpWriter(GxGimexState **, void *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::WriteBmp(GxGimexState *, GxGimexInfo *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::CloseBmpWriter(GxGimexState *) {
  return 0;
}

// Keep the original virtual slot unavailable after removing non-JPEG image-file support.
GxGimexDescriptor *GxGimexCodec::AboutGif() {
  return nullptr;
}

i32 GxGimexCodec::IsGif(void *) {
  return 0;
}

i32 GxGimexCodec::OpenGif(GxGimexState **, void *, char *) {
  return 0;
}

// Keep the original virtual slot unavailable after removing non-JPEG image-file support.
GxGimexInfo *GxGimexCodec::InfoGif(GxGimexState *, i32) {
  return nullptr;
}

i32 GxGimexCodec::ReadGif(GxGimexState *, GxGimexInfo *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::CloseGif(GxGimexState *) {
  return 0;
}

i32 GxGimexCodec::OpenGifWriter(GxGimexState **, void *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::WriteGif(GxGimexState *, GxGimexInfo *, char *, i32) {
  return 0;
}

i32 GxGimexCodec::CloseGifWriter(GxGimexState *) {
  return 0;
}

// Allocate and return the linked JPEG format descriptor.
GxGimexDescriptor *GxGimexCodec::AboutJpeg() {
  return GxGimexDescribeJpeg();
}

i32 GxGimexCodec::IsJpeg(void *file) {
  return GxGimexProbeJpeg(static_cast<GxGimexFile *>(file));
}

i32 GxGimexCodec::OpenJpeg(GxGimexState **out, void *file, char *path) {
  return GxGimexOpenJpegReader(out, static_cast<GxGimexFile *>(file), path);
}

// Return JPEG metadata for the requested image index.
GxGimexInfo *GxGimexCodec::InfoJpeg(GxGimexState *state, i32 index) {
  return GxGimexJpegInfoAt(state, index);
}

i32 GxGimexCodec::ReadJpeg(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch) {
  return GxGimexReadJpeg(state, info, reinterpret_cast<u8 *>(out), pitch);
}

i32 GxGimexCodec::CloseJpeg(GxGimexState *state) {
  return GxGimexCloseState(state);
}

i32 GxGimexCodec::OpenJpegWriter(GxGimexState **out, void *file, char *path, i32 field) {
  return GxGimexCreateWriter(out, static_cast<GxGimexFile *>(file), path, field);
}

i32 GxGimexCodec::WriteJpeg(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch) {
  return GxGimexWriteJpeg(state, info, reinterpret_cast<u8 *>(source), pitch);
}

i32 GxGimexCodec::CloseJpegWriter(GxGimexState *state) {
  return GxGimexCloseState(state);
}

// Allocate and zero one correctly aligned, implicitly created original descriptor record.
static GxGimexDescriptor *GxGimexAllocateDescriptor() {
  u8 *storage = new u8[sizeof(GxGimexDescriptor)]{};
  return reinterpret_cast<GxGimexDescriptor *>(storage);
}

static void GxGimexSetDescriptorFlags(GxGimexDescriptor *description, u32 flags) {
  description->can_import = flags & 1;
  description->can_export = (flags >> 1) & 1;
  description->import_packed = (flags >> 2) & 3;
  description->export_packed = (flags >> 4) & 3;
  description->import_8 = (flags >> 6) & 1;
  description->export_8 = (flags >> 7) & 1;
  description->import_32 = (flags >> 8) & 1;
  description->export_32 = (flags >> 9) & 1;
  description->multiframe = (flags >> 10) & 1;
  description->multifile = (flags >> 11) & 1;
  description->multisize = (flags >> 12) & 1;
  description->framebuffer = (flags >> 13) & 1;
  description->external = (flags >> 14) & 1;
  description->uses_file = (flags >> 15) & 1;
  description->single_palette = (flags >> 16) & 1;
  description->grayscale = (flags >> 17) & 1;
  description->start_color = (flags >> 18) & 1;
  description->do_subtype = (flags >> 19) & 1;
  description->resizable = (flags >> 20) & 1;
  description->pad = flags >> 21;
}

// Select JPEG or the nil entry; unsupported indices select nil.
i32 GX_GIMEX_CALL GxGimexSelect(i32 index) {
  if (index >= kGxGimexFormatCount || index < 0)
    index = 0;
  GxGimexSelection = static_cast<u32>(index);
  return index;
}

u32 GxGimexSelected() {
  return GxGimexSelection;
}

i32 GxGimexCodecCount() {
  return kGxGimexFormatCount;
}

// Allocate the original nil descriptor and publish its fixed identification strings.
GxGimexDescriptor *GxGimexDescribeNil() {
  GxGimexDescriptor *description = GxGimexAllocateDescriptor();
  description->signature = kGxNilCodecSignature;
  description->length = 220;
  description->version = 200;
  GxGimexSetDescriptorFlags(description, 0x1000);
  description->quality = 100;
  std::strcpy(description->author, "FrANK G. Barchard");
  std::strcpy(description->release, "1.02");
  std::strcpy(description->short_name, "Nil");
  std::strcpy(description->type, "Nil");
  std::strcpy(description->description, "Nil Gimex");
  return description;
}

// Allocate the JPEG descriptor with its three supported extensions and original quality value.
GxGimexDescriptor *GxGimexDescribeJpeg() {
  GxGimexDescriptor *description = GxGimexAllocateDescriptor();
  description->signature = kGxJpegSignature;
  description->length = 220;
  description->version = 200;
  GxGimexSetDescriptorFlags(description, 0xb3d7);
  description->quality = 70;
  description->mac_type[0] = kGxJpegSignature;
  std::strcpy(description->extensions[0], ".jpg");
  std::strcpy(description->extensions[1], ".jpeg");
  std::strcpy(description->extensions[2], ".jfif");
  std::strcpy(description->author, "FrANK G. Barchard");
  std::strcpy(description->release, "1.08");
  std::strcpy(description->short_name, "JPG");
  std::strcpy(description->type, "JPeg");
  std::strcpy(description->description, "JPeg");
  return description;
}

// Construct the descriptor for the live module selection without changing that selection.
GxGimexDescriptor *GxGimexDescribe() {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexDescribeJpeg();
  default:
    return GxGimexDescribeNil();
  }
}

u32 GX_GIMEX_CALL GxGimexSelectExtension(const char *extension, i32 *slot) {
  const u8 *input = reinterpret_cast<const u8 *>(extension);
  if (*input == '-' || *input == '+' || *input == '.')
    ++input;
  u32 selected = 0;
  if (*input > 0x20) {
    char token[80];
    u32 length = 1;
    token[0] = '.';
    while (*input > 0x20 && *input != '?') {
      if (length >= sizeof(token) - 1) {
        GxGimexSelect(0);
        return 0;
      }
      token[length++] = static_cast<char>(*input++);
    }
    token[length] = 0;
    for (i32 index = 0; index < 8 && !selected; ++index) {
      for (u32 codec = 1; codec < kGxGimexFormatCount && !selected; ++codec) {
        GxGimexSelect(codec);
        GxGimexDescriptor *description = GxGimexDescribe();
        if (!ScCompareNoCase(token, description->extensions[index])) {
          selected = codec;
          if (slot)
            *slot = index;
        }
        GxGimexFree(description);
      }
    }
  }
  GxGimexSelect(selected);
  return selected;
}

// Construct a descriptor from a fixed table index without mutating the live format selection.
static GxGimexDescriptor *GxGimexDescribeIndex(u32 index) {
  switch (index) {
  case kGxGimexJpeg:
    return GxGimexDescribeJpeg();
  default:
    return GxGimexDescribeNil();
  }
}

// Call the original fixed-index file probe without changing the current format selection.
static i32 GxGimexProbeIndex(u32 index, GxGimexFile *file) {
  switch (index) {
  case kGxGimexJpeg:
    return GxGimexProbeJpeg(file);
  default:
    return 0;
  }
}

// Select the highest-confidence linked format, reducing a file probe when its extension does not match.
// Preserve the first highest score and stop after an exact 100-point result.
i32 GX_GIMEX_CALL GxGimexSelectForFile(const char *path, GxGimexFile *file) {
  i32 best = 0;
  u32 selected = 0;
  for (u32 index = 0; index < kGxGimexFormatCount && best < 100; ++index) {
    GxGimexDescriptor *description = GxGimexDescribeIndex(index);
    i32 score = 0;
    if (!description->uses_file) {
      if (!ScCompareNoCase(path, description->short_name))
        score = 50;
      if (!ScCompareNoCase(path, description->type))
        score = 80;
      if (!ScCompareNoCase(path, description->description))
        score = 95;
    } else if (file) {
      score = GxGimexProbeIndex(index, file);
      if (score > 10) {
        const char *extension = std::strrchr(path, '.');
        bool matched = false;
        if (extension)
          for (u32 slot = 0; slot < 8; ++slot)
            if (!ScCompareNoCase(extension, description->extensions[slot]))
              matched = true;
        if (!matched)
          score -= 10;
      }
    }
    if (score > best) {
      best = score;
      selected = index;
    }
    GxGimexFree(description);
  }
  GxGimexSelect(selected);
  return static_cast<i32>(selected);
}

// Open a requested image or try each original format extension, then select the best reader.
// Preserve the empty resolved path when an existing but unrecognized file is rejected.
GxGimexFile *GX_GIMEX_CALL GxGimexOpenFile(const char *path, char *resolved) {
  usize length = std::strlen(path);
  if (length >= 512) {
    GxGimexSelect(0);
    if (resolved)
      *resolved = 0;
    return nullptr;
  }
  char name[512];
  std::strcpy(name, path);
  GxGimexFile *file = GxGimexOpenReadFile(name);
  if (!file) {
    for (u32 index = 0; index < kGxGimexFormatCount && !file; ++index) {
      GxGimexDescriptor *description = GxGimexDescribeIndex(index);
      if (description->uses_file) {
        for (u32 slot = 0; slot < 8 && !file; ++slot) {
          const char *suffix = description->extensions[slot];
          if (!*suffix)
            continue;
          usize suffix_length = std::strlen(suffix);
          if (length + suffix_length >= sizeof(name)) {
            GxGimexFree(description);
            GxGimexSelect(0);
            if (resolved)
              *resolved = 0;
            return nullptr;
          }
          std::memcpy(name, path, length);
          std::memcpy(name + length, suffix, suffix_length + 1);
          file = GxGimexOpenReadFile(name);
          if (file && !GxGimexProbeIndex(index, file)) {
            GxGimexCloseFile(file);
            file = nullptr;
          }
        }
      }
      GxGimexFree(description);
    }
  }
  u32 selected = file ? static_cast<u32>(GxGimexSelectForFile(path, file)) : 0;
  if (file && !selected) {
    name[0] = 0;
    GxGimexCloseFile(file);
    file = nullptr;
  }
  GxGimexSelect(selected);
  if (resolved)
    std::strcpy(resolved, name);
  return file;
}

// Probe the currently selected reader without changing its module-wide selection.
i32 GX_GIMEX_CALL GxGimexCanRead(GxGimexFile *file) {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexProbeJpeg(file);
  default:
    return 0;
  }
}

// Dispatch the genuine image index and reader state to the selected format's metadata operation.
GxGimexInfo *GX_GIMEX_CALL GxGimexInfoAt(GxGimexState *state, i32 index) {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexJpegInfoAt(state, index);
  default:
    return nullptr;
  }
}

// Forward the original reader, image metadata, output and pitch to the selected decoder.
i32 GX_GIMEX_CALL GxGimexReadPixels(GxGimexState *state, GxGimexInfo *info, u8 *out, i32 pitch) {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexReadJpeg(state, info, out, pitch);
  default:
    return 0;
  }
}

// Forward the original writer, image metadata, input and pitch to the selected encoder.
i32 GX_GIMEX_CALL GxGimexWritePixels(GxGimexState *state, GxGimexInfo *info, u8 *source, i32 pitch) {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexWriteJpeg(state, info, source, pitch);
  default:
    return 0;
  }
}

// Allocate the original reader record and retain its borrowed file without opening or closing it.
static i32 GxGimexCreateReader(GxGimexState **out, GxGimexFile *file, u32 signature) {
  GxGimexState *state = static_cast<GxGimexState *>(GxGimexAlloc(sizeof(GxGimexState)));
  std::memset(state, 0, sizeof(*state));
  state->signature = signature;
  state->size = 24;
  state->frames = 1;
  state->stream = file;
  *out = state;
  return 1;
}

i32 GX_GIMEX_CALL GxGimexOpenJpegReader(GxGimexState **out, GxGimexFile *file, const char *) {
  return GxGimexCreateReader(out, file, kGxJpegSignature);
}

i32 GX_GIMEX_CALL GxGimexOpenReader(GxGimexState **out, GxGimexFile *file, const char *path) {
  switch (GxGimexSelection) {
  case kGxGimexJpeg:
    return GxGimexOpenJpegReader(out, file, path);
  default:
    return 0;
  }
}

i32 GX_GIMEX_CALL GxGimexCloseState(GxGimexState *state) {
  return GxGimexFree(state);
}

i32 GX_GIMEX_CALL GxGimexCloseReader(GxGimexState *state) {
  return GxGimexSelection ? GxGimexCloseState(state) : 0;
}

// Construct the JPEG writer state without retaining or closing its file.
i32 GX_GIMEX_CALL GxGimexCreateWriter(GxGimexState **out, GxGimexFile *file, const char *, i32) {
  GxGimexState *state = static_cast<GxGimexState *>(GxGimexAlloc(sizeof(GxGimexState)));
  std::memset(state, 0, sizeof(*state));
  state->stream = file;
  *out = state;
  return 1;
}

// Dispatch writer initialization according to the module's current format selection.
i32 GX_GIMEX_CALL GxGimexOpenWriter(GxGimexState **out, GxGimexFile *file, const char *path, i32 field) {
  if (GxGimexSelection == kGxGimexJpeg)
    return GxGimexCreateWriter(out, file, path, field);
  return 0;
}

// Close the selected JPEG writer without closing its borrowed file.
i32 GX_GIMEX_CALL GxGimexCloseWriter(GxGimexState *state) {
  if (GxGimexSelection == kGxGimexJpeg)
    return GxGimexCloseState(state);
  return 0;
}

void GX_GIMEX_CALL GxGimexDestroyPixels(GxGimexPixels *pixels) {
  if (pixels) {
    if (reinterpret_cast<u8 *>(pixels->info) != reinterpret_cast<u8 *>(pixels + 1)) {
      if (pixels->image)
        GxGimexFree(pixels->image);
      if (pixels->info)
        GxGimexFree(pixels->info);
    }
    GxGimexFree(pixels);
  }
}

// Open the image, decode the selected frame and return its owned metadata and pixel plane.
// Close reader and file before restoring the previous format selection.
GxGimexPixels *GX_GIMEX_CALL GxGimexDecode(const char *path, i32 index) {
  GxGimexState *state = nullptr;
  GxGimexPixels *pixels = nullptr;
  bool complete = false;
  u32 previous = GxGimexSelected();
  GxGimexFile *file = GxGimexOpenFile(path, nullptr);
  if (file) {
    i32 count = GxGimexOpenReader(&state, file, path);
    if (count) {
      GxGimexDescriptor *description = GxGimexDescribe();
      if (index < count) {
        GxGimexInfo *info = GxGimexInfoAt(state, index);
        if (info) {
          i32 row = static_cast<i32>(static_cast<u32>(info->width) * static_cast<u32>(info->bits));
          i32 size = static_cast<i32>(static_cast<u32>(row) * info->height) / 8;
          u8 *data = static_cast<u8 *>(GxGimexAlloc(size));
          if (data && GxGimexReadPixels(state, info, data, row / 8)) {
            pixels = static_cast<GxGimexPixels *>(GxGimexAlloc(sizeof(GxGimexPixels)));
            pixels->info = info;
            pixels->image = reinterpret_cast<char *>(data);
            pixels->row_bytes = row / 8;
            complete = true;
          }
        }
      }
      GxGimexFree(description);
    }
  }
  if (state)
    GxGimexCloseReader(state);
  if (file)
    GxGimexCloseFile(file);
  if (!complete) {
    GxGimexDestroyPixels(pixels);
    pixels = nullptr;
  }
  GxGimexSelect(previous);
  return pixels;
}

// The actual GIMEX classes are completed separately; their factory functions retain the original direct calls.
ScUnknown *GxCreateGimexImage();
ScUnknown *GxCreateGimexCodec();

GxGimexDirector::GxGimexDirector() {
  AddClass(kGxGimexImageClassId, GxCreateGimexImage, 0);
  AddClass(kGxGimexCodecClassId, GxCreateGimexCodec, 0);
}

GxGimexDirector::~GxGimexDirector() {
}

// Apply the reached common-director start behavior through this module's framework.
// Acquire the registry service, invoke registration and release the temporary service reference.
bool GxGimexDirector::OnStart(ScCom *) {
  void *value = nullptr;
  FrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool GxGimexDirector::OnUnload() {
  return true;
}

u32 GxGimexDirector::GetDirectorID() const {
  return kGxGimexDirectorId;
}

// Construct the static director on first use and return its borrowed primary identity.
ScDirector *GxGetGimexDirector() {
  static GxGimexDirector director;
  return &director;
}

// Expose the same borrowed director to the GZCOM module loader through the DLL's ordinal-one entry.
SC3_C_LINKAGE ScDirector *GxGimexDllGetGZCOMDirector() {
  return GxGetGimexDirector();
}

// The actual static GIMEX director is reconstructed separately with its class registrations.
ScDirector *GxGetGimexDirector();
struct GxGimexFile;
struct GxGimexState;
GxGimexFile *GX_GIMEX_CALL GxGimexOpenFile(const char *path, char *resolved);
i32 GX_GIMEX_CALL GxGimexCanRead(GxGimexFile *file);
i32 GX_GIMEX_CALL GxGimexOpenReader(GxGimexState **out, GxGimexFile *file, const char *path);
GxGimexInfo *GX_GIMEX_CALL GxGimexInfoAt(GxGimexState *state, i32 index);
GxGimexPixels *GX_GIMEX_CALL GxGimexDecode(const char *path, i32 index);
void GX_GIMEX_CALL GxGimexDestroyPixels(GxGimexPixels *pixels);
i32 GX_GIMEX_CALL GxGimexCloseReader(GxGimexState *state);
i32 GX_GIMEX_CALL GxGimexCloseFile(GxGimexFile *file);
u32 GX_GIMEX_CALL GxGimexSelectExtension(const char *extension, i32 *slot);
GxGimexFile *GX_GIMEX_CALL GxGimexCreateFile(const char *path);
i32 GX_GIMEX_CALL GxGimexOpenWriter(GxGimexState **out, GxGimexFile *file, const char *path, i32 field);
i32 GX_GIMEX_CALL GxGimexWritePixels(GxGimexState *state, GxGimexInfo *info, u8 *source, i32 pitch);
i32 GX_GIMEX_CALL GxGimexCloseWriter(GxGimexState *state);

// Borrow the original module-cached graphics service after releasing the temporary service reference.
static GxGraphics *GxGimexGraphics() {
  static GxGraphics *graphics = nullptr;
  if (!graphics) {
    void *value = nullptr;
    GxGetGimexDirector()->FrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    graphics = static_cast<GxGraphics *>(value);
    if (graphics)
      graphics->Release();
  }
  return graphics;
}

GxGimexImage::GxGimexImage() : references(0), graphics(GxGimexGraphics()) {
}

GxGimexImage::~GxGimexImage() {
}

// Publish the image-I/O or base identity before retaining the object; rejected requests clear the output.
bool GxGimexImage::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxGimexImage);
  HANDLE_QUERY_INTERFACE(IID_GxGimexImage, GxGimexImage);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining its actual module director.
u32 GxGimexImage::AddRef() {
  ++references;
  GxGetGimexDirector()->AddRef();
  return references;
}

// Release one director reference for a nonzero object count and delete the object when the count reaches zero.
u32 GxGimexImage::Release() {
  if (references) {
    GxGetGimexDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Construct a codec and return its requested identity. A rejected query discards the unretained object.
u8 GxGimexImage::GetGimexLib(u32 iid, void **out) {
  GxGimexCodec *codec = new GxGimexCodec();
  if (codec->QueryInterface(iid, out))
    return 1;
  delete codec;
  return 0;
}

// Construct the registered image-I/O object with its initial zero reference count.
ScUnknown *GxCreateGimexImage() {
  return new GxGimexImage();
}

// Create a buffer and load the first selected image using the caller's requested color and memory policy.
// Preserve the original cleanup ordering and the reader's missing-metadata early exit.
u8 GxGimexImage::LoadFromFile(const ScStringView &path, GxBuffer **out, GxVramRequest mode, GxColorType format) {
  GxGimexState *state = nullptr;
  ScString name(path);
  GxBuffer *buffer = nullptr;
  u8 result = graphics->CreateBuffer(&buffer);
  if (result) {
    GxGimexFile *file = GxGimexOpenFile(name.begin, nullptr);
    result = file != nullptr;
    if (result && GxGimexCanRead(file)) {
      GxGimexOpenReader(&state, file, name.begin);
      GxGimexInfo *info = GxGimexInfoAt(state, 0);
      if (info) {
        GxGimexPixels *pixels = GxGimexDecode(name.begin, 0);
        result = buffer->Init(
            info->width, info->height, format, nullptr, GxDepthType(GxDepthType::Depth::None), mode, false);
        if (result && buffer->Lock(GxLockFlags::Bits)) {
          result = CopyImage(info, pixels, buffer);
          buffer->Unlock(GxLockFlags::Bits);
        }
        GxGimexDestroyPixels(pixels);
        GxGimexCloseReader(state);
        GxGimexCloseFile(file);
        GxGimexFree(info);
      } else {
        result = false;
      }
    }
  }
  if (!result && buffer) {
    buffer->Release();
    buffer = nullptr;
  }
  if (out)
    *out = buffer;
  else if (buffer)
    buffer->Release();
  return result;
}

u8 GxGimexImage::SaveToFile(const ScStringView &path, GxBuffer *source, u32 quality) {
  ScRect bounds = *source->GetBufferArea();
  return SaveToFile(path, source, &bounds, quality);
}

// Accept the JPEG filename suffixes recognized by the file codec, ignoring ASCII case.
static bool GxGimexHasJpegSuffix(const ScString &name) {
  const char *extension = std::strrchr(name.ToChar(), '.');
  return extension &&
      (!ScCompareNoCase(extension, ".jpg") || !ScCompareNoCase(extension, ".jpeg") ||
          !ScCompareNoCase(extension, ".jfif"));
}

// Require a JPEG filename and write the top-left source pixels with the requested dimensions.
// The original retains its successful lock result and its allocation ownership on conversion failure.
u8 GxGimexImage::SaveToFile(const ScStringView &path, GxBuffer *source, ScRect *bounds, u32 quality) {
  ScString name(path);
  if (!GxGimexHasJpegSuffix(name))
    return false;
  GxGimexState *state = nullptr;
  u8 *storage = static_cast<u8 *>(GxGimexAlloc(kGxGimexRetailInfoSize));
  std::memset(storage, 0, kGxGimexRetailInfoSize);
  GxGimexInfo *info = reinterpret_cast<GxGimexInfo *>(storage);
  info->signature = kGxJpegSignature;
  info->length = kGxGimexRetailInfoSize;
  info->version = 200;
  info->width = static_cast<u32>(bounds->right) - static_cast<u32>(bounds->left);
  info->height = static_cast<u32>(bounds->bottom) - static_cast<u32>(bounds->top);
  info->compressed = 1;
  info->quality = quality;
  info->red_bits = info->green_bits = info->blue_bits = 8;
  info->bits = info->source_bits = 32;
  if (!source->Lock(GxLockFlags::Bits))
    return false;
  GxGimexPixels pixels = {info, static_cast<char *>(GxGimexAlloc(static_cast<i32>(4u * info->width * info->height))),
      static_cast<i32>(4u * info->width)};
  u8 copied = TranslateSourceBufferToGimex(source, info, &pixels);
  source->Unlock(GxLockFlags::Bits);
  if (copied == 1) {
    GxGimexSelectExtension("JPG", nullptr);
    GxGimexFile *file = GxGimexCreateFile(name.begin);
    if (file) {
      GxGimexOpenWriter(&state, file, name.begin, 1);
      GxGimexWritePixels(state, info, reinterpret_cast<u8 *>(pixels.image),
          static_cast<i32>(static_cast<u32>(info->width) * static_cast<u32>(info->bits)) / 8);
      GxGimexCloseWriter(state);
      GxGimexCloseFile(file);
    }
    GxGimexFree(info);
    GxGimexFree(pixels.image);
  }
  return true;
}

u8 GxGimexImage::IsBlackBuffer(GxBuffer *source) {
  ScRect bounds = *source->GetBufferArea();
  return IsBlackBufferRect(source, &bounds);
}

// Reject out-of-range inclusive bounds and non-16-bit buffers, then compare locked pixels with native black.
// A failed lock retains the original successful result.
u8 GxGimexImage::IsBlackBufferRect(GxBuffer *source, ScRect *bounds) {
  if (bounds->left < 0 || bounds->right >= static_cast<i32>(source->Width()) || bounds->top < 0 ||
      bounds->bottom >= static_cast<i32>(source->Height()))
    return false;
  if (source->GetBitsPerPixel() != 16)
    return false;
  u16 black = source->ConvertRGBValueToNative16Bit(0, 0, 0);
  bool result = true;
  if (source->Lock(GxLockFlags::Bits)) {
    for (i32 y = bounds->top; y <= bounds->bottom; ++y) {
      for (i32 x = bounds->left; x <= bounds->right; ++x) {
        if (source->GetPixel(x, y) != black) {
          result = false;
          break;
        }
      }
      if (!result)
        break;
    }
    source->Unlock(GxLockFlags::Bits);
  }
  return result;
}

// Dispatch supported indexed or BGRA source pixels only when the destination uses 16-bit color.
u8 GxGimexImage::CopyImage(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out) {
  if (!info || !pixels)
    return false;
  i32 bits = out->GetBitsPerPixel();
  if (info->bits == 8 && bits == 16)
    return CopyIndexed(info, pixels, out);
  if (info->bits == 32 && bits == 16)
    return CopyColor(info, pixels, out);
  return false;
}

// Convert indexed pixels through the live graphics service, caching each palette entry's native color.
// BMP rows run bottom to top; other decoded rows run forward using the destination's even-byte pitch.
u8 GxGimexImage::CopyIndexed(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out) {
  u32 colors[256];
  for (u32 &color : colors)
    color = kGxUnconvertedPaletteEntry;
  i32 width = info->width, height = info->height;
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = reinterpret_cast<const u8 *>(pixels->image);
  isize step = pitch;
  if (info->signature == kGxBitmapCodecSignature && height > 0) {
    row += static_cast<usize>(pitch) * (height - 1);
    step = -static_cast<isize>(pitch);
  }
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 index = *source++;
      u32 *color = colors + index;
      if (static_cast<i32>(*color) < 0) {
        const GxGimexColor &value = info->palette[index];
        *color = graphics->ConvertRGBValueToNative16Bit(value.red, value.green, value.blue);
      }
      target[0] = static_cast<u8>(*color);
      target[1] = static_cast<u8>(*color >> 8);
      target += 2;
    }
    if (y + 1 < height)
      row += step;
  }
  return true;
}

// Convert consecutive BGRA pixels to the display's native WORD format, ignoring each source alpha byte.
u8 GxGimexImage::CopyColor(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out) {
  i32 width = info->width, height = info->height;
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = reinterpret_cast<const u8 *>(pixels->image);
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 blue = source[0], green = source[1], red = source[2];
      source += 4;
      u16 value = graphics->ConvertRGBValueToNative16Bit(red, green, blue);
      target[0] = static_cast<u8>(value);
      target[1] = static_cast<u8>(value >> 8);
      target += 2;
    }
    if (y + 1 < height)
      row += pitch;
  }
  return true;
}

// Require both output records and a 16-bit source before dispatching the original color-reader operation.
u8 GxGimexImage::TranslateSourceBufferToGimex(GxBuffer *source, GxGimexInfo *info, GxGimexPixels *pixels) {
  if (info && pixels && source->GetBitsPerPixel() == 16)
    return Translate16BufferToGimex(source, info, pixels);
  return 0;
}

// Read top-left source pixels into consecutive BGRA output, retaining the original reused channel storage.
// The buffer converter supplies the RGB bytes before each pixel is written with zero alpha.
u8 GxGimexImage::Translate16BufferToGimex(GxBuffer *source, GxGimexInfo *info, GxGimexPixels *pixels) {
  i32 width = info->width, height = info->height;
  if (source->GetBitsPerPixel() != 16)
    return 0;
  u8 *out = reinterpret_cast<u8 *>(pixels->image);
  u8 red, green, blue;
  for (i32 y = 0; y < height; ++y) {
    for (i32 x = 0; x < width; ++x) {
      u16 pixel = static_cast<u16>(source->GetPixel(x, y));
      source->Convert16BitNativeValueToRGB(pixel, &red, &green, &blue);
      out[0] = blue;
      out[1] = green;
      out[2] = red;
      out[3] = 0;
      out += 4;
    }
  }
  return 1;
}

void GxGimexConvertJpegRow(
    const u8 *source, u8 *out, i32 count, i32 components, GxJpegColorSpace output_space, i32 four_channel);

// GIMEX extends the IJG source manager with its stream, read buffer and first-read flag.
struct GxGimexJpegSource {
  GxJpegSource manager;
  GxGimexFile *file;
  u8 *buffer;
  i32 start_of_file;
};

// GIMEX extends the IJG destination manager with its stream and output buffer.
struct GxGimexJpegDestination {
  GxJpegDestination manager;
  GxGimexFile *file;
  u8 *buffer;
};

i32 GxGimexJpegArgbMarker = 0;

// GIMEX appends its recovery context to the native IJG error manager.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // The native jmp_buf may require padding after IJG's error manager.
#endif
struct GxGimexJpegError {
  GxJpegErrorManager manager;
  std::jmp_buf recovery;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Keep the original JPEG objects together across the nonlocal error return.
struct GxGimexJpegProbe {
  GxJpegDecoder decoder;
  GxGimexJpegError error;
};

static void GxGimexJpegErrorExit(GxJpegCommon *common) {
  GxGimexJpegError *error = reinterpret_cast<GxGimexJpegError *>(common->err);
  std::longjmp(error->recovery, 1);
}

static void GxGimexInitJpegSource(GxJpegDecoder *decoder) {
  GxGimexJpegSource *source = reinterpret_cast<GxGimexJpegSource *>(decoder->src);
  source->start_of_file = 1;
}

// Refill the JPEG source from its GIMEX stream, synthesizing the original end marker after an exhausted image.
static i32 GxGimexFillJpegSource(GxJpegDecoder *decoder) {
  GxGimexJpegSource *source = reinterpret_cast<GxGimexJpegSource *>(decoder->src);
  i32 count = GxGimexReadBytes(source->file, source->buffer, 4096);
  if (!count) {
    GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(decoder);
    if (source->start_of_file) {
      decoder->err->msg_code = 41;
      decoder->err->error_exit(common);
    }
    decoder->err->msg_code = 116;
    decoder->err->emit_message(common, -1);
    source->buffer[0] = 0xff;
    source->buffer[1] = 0xd9;
    count = 2;
  }
  source->start_of_file = 0;
  source->manager.bytes_in_buffer = static_cast<usize>(count);
  source->manager.next_input_byte = source->buffer;
  return 1;
}

// Skip compressed input across as many GIMEX-backed JPEG buffer refills as required.
static void GxGimexSkipJpegInput(GxJpegDecoder *decoder, long count) {
  GxGimexJpegSource *source = reinterpret_cast<GxGimexJpegSource *>(decoder->src);
  if (count <= 0)
    return;
  while (count > static_cast<long>(source->manager.bytes_in_buffer)) {
    count -= static_cast<long>(source->manager.bytes_in_buffer);
    GxGimexFillJpegSource(decoder);
  }
  source->manager.next_input_byte += count;
  source->manager.bytes_in_buffer -= static_cast<usize>(count);
}

static void GxGimexFinishJpegSource(GxJpegDecoder *) {
}

void GxGimexAttachJpegSource(GxJpegDecoder *decoder, GxGimexFile *file) {
  GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(decoder);
  if (!decoder->src) {
    GxGimexJpegSource *source =
        static_cast<GxGimexJpegSource *>(decoder->mem->alloc_small(common, 0, sizeof(GxGimexJpegSource)));
    decoder->src = &source->manager;
    source->buffer = static_cast<u8 *>(decoder->mem->alloc_small(common, 0, 4096));
  }
  GxGimexJpegSource *source = reinterpret_cast<GxGimexJpegSource *>(decoder->src);
  source->manager.bytes_in_buffer = 0;
  source->manager.next_input_byte = nullptr;
  source->manager.init_source = GxGimexInitJpegSource;
  source->manager.fill_input_buffer = GxGimexFillJpegSource;
  source->manager.skip_input_data = GxGimexSkipJpegInput;
  source->manager.resync_to_restart = GxJpeg::jpeg_resync_to_restart;
  source->manager.term_source = GxGimexFinishJpegSource;
  source->file = file;
}

// Allocate the original image-pool JPEG output buffer and make it the active destination window.
static void GxGimexInitJpegDestination(GxJpegEncoder *encoder) {
  GxGimexJpegDestination *destination = reinterpret_cast<GxGimexJpegDestination *>(encoder->dest);
  GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(encoder);
  destination->buffer = static_cast<u8 *>(encoder->mem->alloc_small(common, 1, 4096));
  destination->manager.next_output_byte = destination->buffer;
  destination->manager.free_in_buffer = 4096;
}

static i32 GxGimexEmptyJpegDestination(GxJpegEncoder *encoder) {
  GxGimexJpegDestination *destination = reinterpret_cast<GxGimexJpegDestination *>(encoder->dest);
  if (GxGimexWriteBytes(destination->file, destination->buffer, 4096) != 4096) {
    encoder->err->msg_code = 36;
    encoder->err->error_exit(reinterpret_cast<GxJpegCommon *>(encoder));
  }
  destination->manager.next_output_byte = destination->buffer;
  destination->manager.free_in_buffer = 4096;
  return 1;
}

// Flush the remaining JPEG output bytes and preserve the original short-write error callback.
static void GxGimexFinishJpegDestination(GxJpegEncoder *encoder) {
  GxGimexJpegDestination *destination = reinterpret_cast<GxGimexJpegDestination *>(encoder->dest);
  u32 count = 4096 - static_cast<u32>(destination->manager.free_in_buffer);
  if (count && GxGimexWriteBytes(destination->file, destination->buffer, count) != count) {
    encoder->err->msg_code = 36;
    encoder->err->error_exit(reinterpret_cast<GxJpegCommon *>(encoder));
  }
}

// Install the original permanent-pool JPEG destination manager for a borrowed GIMEX stream.
static void GxGimexAttachJpegDestination(GxJpegEncoder *encoder, GxGimexFile *file) {
  if (!encoder->dest) {
    GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(encoder);
    GxGimexJpegDestination *destination =
        static_cast<GxGimexJpegDestination *>(encoder->mem->alloc_small(common, 0, sizeof(GxGimexJpegDestination)));
    encoder->dest = &destination->manager;
  }
  GxGimexJpegDestination *destination = reinterpret_cast<GxGimexJpegDestination *>(encoder->dest);
  destination->manager.init_destination = GxGimexInitJpegDestination;
  destination->manager.empty_output_buffer = GxGimexEmptyJpegDestination;
  destination->manager.term_destination = GxGimexFinishJpegDestination;
  destination->file = file;
}

// This frame has only scalar locals; JPEG state lives outside it and no C++ destructor is crossed.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
// Probe the header while the JPEG objects remain outside the nonlocal-return stack frame.
static i32 GxGimexProbeJpegWithRecovery(GxGimexJpegProbe *probe, GxGimexFile *file) {
  volatile i32 result = 0;
  GxGimexSeek(file, 0);
  probe->decoder.err = GxJpeg::jpeg_std_error(&probe->error.manager);
  probe->error.manager.error_exit = GxGimexJpegErrorExit;
  if (setjmp(probe->error.recovery) == 0) {
    GxJpeg::jpeg_CreateDecompress(&probe->decoder, JPEG_LIB_VERSION, sizeof(probe->decoder));
    GxGimexAttachJpegSource(&probe->decoder, file);
    if (GxJpeg::jpeg_read_header(&probe->decoder, 1) == 1)
      result = 100;
  }
  GxJpeg::jpeg_destroy_decompress(&probe->decoder);
  return result;
}

// Read the original JPEG dimensions and marker state while retaining IJG's nonlocal error handling.
// The decoder and error records are owned outside this scalar-only recovery frame.
static GxGimexInfo *GxGimexJpegInfoWithRecovery(GxGimexJpegProbe *probe, GxGimexState *state) {
  GxJpegDecoder *decoder = &probe->decoder;
  decoder->err = GxJpeg::jpeg_std_error(&probe->error.manager);
  probe->error.manager.error_exit = GxGimexJpegErrorExit;
  if (setjmp(probe->error.recovery) != 0) {
    GxJpeg::jpeg_destroy_decompress(decoder);
    return nullptr;
  }
  GxJpeg::jpeg_CreateDecompress(decoder, JPEG_LIB_VERSION, sizeof(*decoder));
  GxJpeg::jpeg_set_marker_processor(decoder, 237, GxGimexJpegMarker);
  GxGimexFile *file = static_cast<GxGimexFile *>(state->stream);
  GxGimexSeek(file, 0);
  GxGimexAttachJpegSource(decoder, file);
  GxGimexJpegArgbMarker = 0;
  GxJpeg::jpeg_read_header(decoder, 1);
  i32 marker = GxGimexJpegArgbMarker;
  GxJpeg::jpeg_start_decompress(decoder);
  u32 width = decoder->output_width, height = decoder->output_height;
  i32 components = decoder->output_components;
  GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(decoder);
  u8 **row = decoder->mem->alloc_sarray(common, 1, width * static_cast<u32>(components), 1);
  while (decoder->output_scanline < height)
    GxJpeg::jpeg_read_scanlines(decoder, row, 1);
  GxJpeg::jpeg_finish_decompress(decoder);
  GxJpeg::jpeg_destroy_decompress(decoder);
  GxGimexInfo *info = static_cast<GxGimexInfo *>(GxGimexAlloc(sizeof(GxGimexInfo)));
  std::memset(info, 0, sizeof(*info));
  info->signature = kGxJpegSignature;
  info->length = kGxGimexLegacyInfoSize;
  info->version = 200;
  info->width = static_cast<i32>(width);
  info->height = static_cast<i32>(height);
  info->subtype = marker != 0;
  info->compressed = 1;
  info->quality = 70;
  info->red_bits = info->green_bits = info->blue_bits = 8;
  if (components == 1) {
    info->bits = info->source_bits = 8;
    info->palette_count = 256;
    for (i32 index = 0; index < 256; ++index) {
      u8 color = static_cast<u8>(index);
      info->palette[index] = {color, color, color, 255};
    }
  } else {
    info->bits = 32;
    info->source_bits = 24;
  }
  return info;
}

// Select the original output color space for YCbCr and YCCK input before decompression starts.
static void GxGimexNormalizeJpegColorSpace(GxJpegDecoder *decoder) {
  if (decoder->jpeg_color_space == GxJpeg::JCS_YCbCr)
    decoder->out_color_space = GxJpeg::JCS_RGB;
  else if (decoder->jpeg_color_space == GxJpeg::JCS_YCCK)
    decoder->out_color_space = GxJpeg::JCS_CMYK;
}

// Decode and convert JPEG scanlines while retaining IJG's original recoverable error handling.
// The decoder and error records are owned outside this scalar-only recovery frame.
static i32 GxGimexReadJpegWithRecovery(
    GxGimexJpegProbe *probe, GxGimexState *state, GxGimexInfo *info, u8 *out, i32 pitch) {
  GxJpegDecoder *decoder = &probe->decoder;
  decoder->err = GxJpeg::jpeg_std_error(&probe->error.manager);
  probe->error.manager.error_exit = GxGimexJpegErrorExit;
  if (setjmp(probe->error.recovery) != 0) {
    GxJpeg::jpeg_destroy_decompress(decoder);
    return 0;
  }
  GxJpeg::jpeg_CreateDecompress(decoder, JPEG_LIB_VERSION, sizeof(*decoder));
  GxJpeg::jpeg_set_marker_processor(decoder, 237, GxGimexJpegMarker);
  GxGimexFile *file = static_cast<GxGimexFile *>(state->stream);
  GxGimexSeek(file, 0);
  GxGimexAttachJpegSource(decoder, file);
  GxGimexJpegArgbMarker = 0;
  GxJpeg::jpeg_read_header(decoder, 1);
  i32 marker = GxGimexJpegArgbMarker;
  GxGimexNormalizeJpegColorSpace(decoder);
  GxJpeg::jpeg_start_decompress(decoder);
  u32 width = decoder->output_width;
  i32 height = static_cast<i32>(decoder->output_height), components = decoder->output_components;
  if (width > INT32_MAX) {
    GxJpeg::jpeg_destroy_decompress(decoder);
    return 0;
  }
  GxJpegCommon *common = reinterpret_cast<GxJpegCommon *>(decoder);
  u8 **row = decoder->mem->alloc_sarray(common, 1, width * static_cast<u32>(components), 1);
  // Exclude indeterminate first-use bytes; later suspended reads retain the actual preceding row.
  std::memset(*row, 0, width * static_cast<u32>(components));
  for (i32 y = 0; y < height; ++y) {
    GxJpeg::jpeg_read_scanlines(decoder, row, 1);
    if (y < info->height) {
      i32 required = marker ? 4 : decoder->out_color_space == GxJpeg::JCS_GRAYSCALE ? 1
          : decoder->out_color_space == GxJpeg::JCS_CMYK ? 4 : 3;
      if (width && components < required) {
        GxJpeg::jpeg_destroy_decompress(decoder);
        return 0;
      }
      GxGimexConvertJpegRow(*row, out, static_cast<i32>(width), components, decoder->out_color_space, marker);
      if (y + 1 < height && y + 1 < info->height)
        out += pitch;
    }
  }
  GxJpeg::jpeg_finish_decompress(decoder);
  GxJpeg::jpeg_destroy_decompress(decoder);
  return 1;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Probe a file with the original JPEG 6a header reader and return its 100-point match score.
// The decoder and error state remain allocated across IJG's nonlocal error return.
i32 GX_GIMEX_CALL GxGimexProbeJpeg(GxGimexFile *file) {
  GxGimexJpegProbe *probe = new GxGimexJpegProbe;
  i32 result = GxGimexProbeJpegWithRecovery(probe, file);
  delete probe;
  return result;
}

// Read JPEG metadata through the original version-61 decompressor and its GIMEX marker handler.
// The decoder and error state remain outside the nonlocal-return stack frame.
GxGimexInfo *GX_GIMEX_CALL GxGimexJpegInfoAt(GxGimexState *state, i32) {
  GxGimexJpegProbe *probe = new GxGimexJpegProbe;
  GxGimexInfo *info = GxGimexJpegInfoWithRecovery(probe, state);
  delete probe;
  return info;
}

// Read the original JPEG frame into the caller's buffer through the actual scanline converter.
// The decoder and error state remain outside the nonlocal-return stack frame.
i32 GX_GIMEX_CALL GxGimexReadJpeg(GxGimexState *state, GxGimexInfo *info, u8 *out, i32 pitch) {
  GxGimexJpegProbe *probe = new GxGimexJpegProbe;
  i32 result = GxGimexReadJpegWithRecovery(probe, state, info, out, pitch);
  delete probe;
  return result;
}

// Consume a marker byte through the actual JPEG source manager, retaining local cursor state until commit.
// IJG 6a requires a successful refill to supply bytes; suspension returns zero without consuming input.
static i32 GxGimexReadJpegMarkerByte(GxJpegDecoder *decoder, const u8 **next, usize *remaining, u8 *out) {
  if (!*remaining) {
    GxJpegSource *source = decoder->src;
    if (!source->fill_input_buffer(decoder))
      return 0;
    *next = source->next_input_byte;
    *remaining = source->bytes_in_buffer;
    assert(*remaining && "JPEG source refill succeeded without supplying bytes");
  }
  *out = *(*next)++;
  --*remaining;
  return 1;
}

// Parse the original GIMEXARGB JPEG marker and skip the remaining marker payload through IJG's source manager.
i32 GxGimexJpegMarker(GxJpegDecoder *decoder) {
  GxJpegSource *source = decoder->src;
  const u8 *next = source->next_input_byte;
  usize remaining = source->bytes_in_buffer;
  u8 high, low;
  if (!GxGimexReadJpegMarkerByte(decoder, &next, &remaining, &high) ||
      !GxGimexReadJpegMarkerByte(decoder, &next, &remaining, &low))
    return 0;
  i32 payload = (static_cast<i32>(high) << 8) + low - 2;
  if (payload < 9)
    return 0;
  u8 signature[9];
  for (u32 index = 0; index < sizeof(signature); ++index)
    if (!GxGimexReadJpegMarkerByte(decoder, &next, &remaining, &signature[index]))
      return 0;
  payload -= 9;
  if (!std::memcmp(signature, "GIMEXARGB", sizeof(signature)))
    GxGimexJpegArgbMarker = 1;
  source->next_input_byte = next;
  source->bytes_in_buffer = remaining;
  if (payload > 0)
    source->skip_input_data(decoder, payload);
  return 1;
}

// Convert one CMYK sample using the original saturated channel-plus-black operation.
static void GxGimexConvertJpegCmyk(const u8 *source, u8 *out) {
  u32 black = source[3], cyan = source[0] + black, magenta = source[1] + black, yellow = source[2] + black;
  out[0] = static_cast<u8>(255 - (cyan >= 255 ? 255 : cyan));
  out[1] = static_cast<u8>(255 - (magenta >= 255 ? 255 : magenta));
  out[2] = static_cast<u8>(255 - (yellow >= 255 ? 255 : yellow));
}

// Convert one JPEG scanline into the original indexed, BGR or four-channel output layout. The connected decoder bounds
// the width before this effects-only conversion receives it.
void GxGimexConvertJpegRow(const u8 *source, u8 *out, i32 count, i32, GxJpegColorSpace output_space, i32 four_channel) {
  if (four_channel) {
    for (i32 index = 0; index < count; ++index) {
      out[3] = source[0];
      out[2] = source[1];
      out[1] = source[2];
      out[0] = source[3];
      source += 4;
      out += 4;
    }
  } else if (output_space == GxJpeg::JCS_GRAYSCALE) {
    if (count >= 0)
      std::memcpy(out, source, static_cast<usize>(count));
  } else if (output_space == GxJpeg::JCS_CMYK) {
    for (i32 index = 0; index < count; ++index) {
      u8 rgb[3];
      GxGimexConvertJpegCmyk(source, rgb);
      out[3] = 255;
      out[2] = rgb[0];
      out[1] = rgb[1];
      out[0] = rgb[2];
      source += 4;
      out += 4;
    }
  } else {
    for (i32 index = 0; index < count; ++index) {
      out[3] = 255;
      out[2] = source[0];
      out[1] = source[1];
      out[0] = source[2];
      source += 3;
      out += 4;
    }
  }
}

// Convert source pixels into the writer's four-channel, grayscale or RGB JPEG scanline.
void GxGimexPrepareJpegRow(u8 *source, u8 *out, i32 count, i32 mode, GxGimexInfo *info) {
  if (mode == 0) {
    if (info->bits == 32) {
      for (i32 index = 0; index < count; ++index) {
        out[0] = source[3];
        out[1] = source[2];
        out[2] = source[1];
        out[3] = source[0];
        source += 4;
        out += 4;
      }
    } else {
      for (i32 index = 0; index < count; ++index) {
        out[0] = info->palette[*source].alpha;
        out[1] = info->palette[*source].red;
        out[2] = info->palette[*source].green;
        out[3] = info->palette[*source++].blue;
        out += 4;
      }
    }
  } else if (mode == 1) {
    if (info->bits == 32) {
      for (i32 index = 0; index < count; ++index) {
        *out++ = source[1];
        source += 4;
      }
    } else {
      for (i32 index = 0; index < count; ++index)
        out[index] = info->palette[source[index]].green;
    }
  } else if (mode == 2) {
    if (info->bits == 32) {
      for (i32 index = 0; index < count; ++index) {
        out[0] = source[2];
        out[1] = source[1];
        out[2] = source[0];
        source += 4;
        out += 3;
      }
    } else {
      for (i32 index = 0; index < count; ++index) {
        out[0] = info->palette[*source].red;
        out[1] = info->palette[*source].green;
        out[2] = info->palette[*source].blue;
        ++source;
        out += 3;
      }
    }
  }
}

// Check RGB channels in each four-byte color and stop at the first mismatch.
// Nonpositive counts return the original 32-bit one without reading input.
static i32 GxGimexJpegIsGrayscale(u8 *source, i32 count) {
  for (i32 index = 0; index < count; ++index, source += 4)
    if (source[2] != source[1] || source[2] != source[0])
      return 0;
  return 1;
}

// Write JPEG pixels through the original IJG 6a compressor and GIMEX-backed destination.
// Preserve the default JPEG error handler, optional ARGB marker and original grayscale selection.
i32 GX_GIMEX_CALL GxGimexWriteJpeg(GxGimexState *state, GxGimexInfo *info, u8 *source, i32 pitch) {
  i32 width = info->width, height = info->height, components = 3;
  GxJpegColorSpace color_space = GxJpeg::JCS_RGB;
  if (info->subtype & 1) {
    color_space = GxJpeg::JCS_UNKNOWN;
    components = 4;
  } else if (info->bits == 8) {
    if (GxGimexJpegIsGrayscale(reinterpret_cast<u8 *>(info) + offsetof(GxGimexInfo, palette), info->palette_count)) {
      color_space = GxJpeg::JCS_GRAYSCALE;
      components = 1;
    }
  } else {
    bool grayscale = true;
    u8 *row = source;
    for (i32 y = 0; y < height; ++y) {
      if (!GxGimexJpegIsGrayscale(row, width)) {
        grayscale = false;
        break;
      }
      if (y + 1 < height)
        row += pitch;
    }
    if (grayscale) {
      color_space = GxJpeg::JCS_GRAYSCALE;
      components = 1;
    }
  }
  GxJpegEncoder encoder;
  GxJpegErrorManager error;
  encoder.err = GxJpeg::jpeg_std_error(&error);
  GxJpeg::jpeg_CreateCompress(&encoder, JPEG_LIB_VERSION, sizeof(encoder));
  GxGimexAttachJpegDestination(&encoder, static_cast<GxGimexFile *>(state->stream));
  encoder.image_width = static_cast<u32>(width);
  encoder.image_height = static_cast<u32>(height);
  encoder.input_components = components;
  encoder.in_color_space = color_space;
  GxJpeg::jpeg_set_defaults(&encoder);
  GxJpeg::jpeg_set_quality(&encoder, info->quality, 1);
  GxJpeg::jpeg_start_compress(&encoder, 1);
  if (info->subtype & 1)
    GxJpeg::jpeg_write_marker(&encoder, 237, reinterpret_cast<const u8 *>("GIMEXARGB"), 9);
  i32 size = static_cast<i32>(static_cast<u32>(width) * static_cast<u32>(components));
  u8 *row = static_cast<u8 *>(GxGimexAlloc(size));
  i32 result = 0;
  if (row) {
    for (i32 y = 0; y < static_cast<i32>(encoder.image_height); ++y) {
      GxGimexPrepareJpegRow(source, row, width, static_cast<i32>(color_space), info);
      u8 *scanline = row;
      GxJpeg::jpeg_write_scanlines(&encoder, &scanline, 1);
      if (y + 1 < static_cast<i32>(encoder.image_height))
        source += pitch;
    }
    GxGimexFree(row);
    result = 1;
  }
  GxJpeg::jpeg_finish_compress(&encoder);
  GxJpeg::jpeg_destroy_compress(&encoder);
  return result;
}

// Allocate native codec storage without reproducing the original general-purpose sentinel allocator.
// The original nonpositive-length case returns null before allocating.
void *GX_GIMEX_CALL GxGimexAlloc(i32 length) {
  if (length <= 0)
    return nullptr;
  return new u8[static_cast<u32>(length)];
}

// Release storage allocated by the codec and retain its original zero result for a null input.
i32 GX_GIMEX_CALL GxGimexFree(void *allocation) {
  if (!allocation)
    return 0;
  delete[] static_cast<u8 *>(allocation);
  return 1;
}

// Open a binary input stream and initialize its logical position and empty buffered read window.
GxGimexFile *GX_GIMEX_CALL GxGimexOpenReadFile(const char *path) {
  FILE *stream = !std::strncmp(path, "con:", 4) ? stdin : ScOpenNativeFile(path, "rb");
  if (!stream)
    return nullptr;
  std::fseek(stream, 0, SEEK_END);
  i32 length = static_cast<i32>(std::ftell(stream));
  std::fseek(stream, 0, SEEK_SET);
  GxGimexFile *file = static_cast<GxGimexFile *>(GxGimexAlloc(sizeof(GxGimexFile)));
  file->stream = stream;
  file->position = 0;
  file->length = length;
  file->read_ptr = file->buffer;
  file->read_end = file->buffer;
  file->write_mode = 0;
  file->field_018 = 0;
  return file;
}

// Open the original writable stream, falling back from update mode to write-only mode.
GxGimexFile *GX_GIMEX_CALL GxGimexCreateFile(const char *path) {
  FILE *stream = nullptr;
  if (!std::strncmp(path, "con:", 4))
    stream = stdout;
  else {
    stream = ScOpenNativeFile(path, "w+b");
    if (!stream)
      stream = ScOpenNativeFile(path, "wb");
  }
  if (!stream)
    return nullptr;
  GxGimexFile *file = static_cast<GxGimexFile *>(GxGimexAlloc(sizeof(GxGimexFile)));
  file->stream = stream;
  file->position = 0;
  file->length = 0;
  file->read_ptr = file->buffer;
  file->read_end = file->buffer;
  file->write_mode = 1;
  file->field_018 = 0;
  return file;
}

// Close the real file before releasing its descriptor; null retains the original successful result.
i32 GX_GIMEX_CALL GxGimexCloseFile(GxGimexFile *file) {
  if (!file)
    return 1;
  i32 closed = std::fclose(file->stream) == 0;
  return GxGimexFree(file) & closed;
}

i32 GX_GIMEX_CALL GxGimexReadBytes(GxGimexFile *file, u8 *out, i32 length) {
  i32 total = 0;
  while (length > 0) {
    iptr available = static_cast<iptr>(reinterpret_cast<uptr>(file->read_end) - reinterpret_cast<uptr>(file->read_ptr));
    i32 take = available > length ? length : static_cast<i32>(available);
    if (take > 0) {
      std::memcpy(out, file->read_ptr, static_cast<u32>(take));
      file->read_ptr += take;
      file->position += take;
      out += take;
      length -= take;
      total += take;
    }
    if (file->read_end == file->read_ptr) {
      if (!length)
        return total;
      i32 remaining = file->length - file->position;
      file->read_ptr = file->buffer;
      file->read_end = file->buffer;
      if (remaining > 8192)
        remaining = 8192;
      if (remaining <= 0)
        return total;
      usize read = std::fread(file->buffer, 1, static_cast<u32>(remaining), file->stream);
      if (!read)
        return total;
      file->read_end += read;
    }
  }
  return total;
}

// Advance the logical cursor before writing and retain the original maximum logical length.
u32 GX_GIMEX_CALL GxGimexWriteBytes(GxGimexFile *file, const void *source, u32 length) {
  file->position = static_cast<i32>(static_cast<u32>(file->position) + length);
  if (file->position > file->length)
    file->length = file->position;
  return static_cast<u32>(std::fwrite(source, 1, length, file->stream));
}

// Reuse a valid buffered position or reset the window before seeking the real file.
i32 GX_GIMEX_CALL GxGimexSeek(GxGimexFile *file, i32 position) {
  bool valid = true;
  if (position < 0 || (!file->write_mode && position > file->length)) {
    position = position < 0 ? 0 : file->length;
    valid = false;
    file->read_ptr = file->buffer;
    file->read_end = file->buffer;
  } else {
    file->read_ptr = reinterpret_cast<u8 *>(reinterpret_cast<uptr>(file->read_ptr) +
        static_cast<iptr>(static_cast<i32>(static_cast<u32>(position) - static_cast<u32>(file->position))));
  }
  file->position = position;
  if (file->write_mode)
    return std::fseek(file->stream, position, SEEK_SET) == 0 && valid;
  iptr available = static_cast<iptr>(reinterpret_cast<uptr>(file->read_end) - reinterpret_cast<uptr>(file->read_ptr));
  iptr offset = static_cast<iptr>(reinterpret_cast<uptr>(file->read_ptr) - reinterpret_cast<uptr>(file->buffer));
  if (available <= 0 || available > 8192 || offset < 0) {
    valid = position >= 0 && position <= file->length && std::fseek(file->stream, position, SEEK_SET) == 0;
    file->read_ptr = file->buffer;
    file->read_end = file->buffer;
  }
  return valid;
}

i32 GX_GIMEX_CALL GxGimexTell(GxGimexFile *file) {
  return file->position;
}
