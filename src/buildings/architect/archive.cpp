#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_archive.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/manager.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/locale.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "resources/stream.h"

// Document records preserve the building description, building type, grid mode bytes and registration/identity data.
enum BdDocumentAuxiliaryKey : u32 {
  kBdDocumentDescriptionRecordType = 0x644d7178,
  kBdDocumentDescriptionDefaultInstance = 0xa44d71aa,
  kBdDocumentBuildingTypeRecordType = 0x644d7179,
  kBdDocumentBuildingTypeDefaultInstance = 0xa44d71ab,
  kBdDocumentModeRecordType = 0x99f88b9e,
  kBdDocumentModeDefaultInstance = 0xb9f88bd2,
  kBdDocumentRegistrationRecordType = 0xda6dc9a9,
};

// Stored textures retain their transparency word; the legacy last-texture identifier resolves to its original name.
enum BdStoredImageConstant : u32 {
  kBdStoredImageTransparencyColor = 0xffff00ff,
  kBdLegacyLastTextureId = 0xffffffff,
};

// Asset images and document language use separate records. Grid compression and indexed decoding retain their limit/flag.
enum BdArchiveFormatValue : u32 {
  kBdAssetImageRecordType = 0xe3f87b10,
  kBdDocumentLanguageRecordType = 0xb9ec8f72,
  kBdDocumentLanguageDefaultInstance = 0x99ec9601,
  kBdGridCompressedCapacityLimit = 0x400000,
  kBdImageIndexedFormatFlag = 0x10000000,
};

// Image archive encoding selectors dispatch raw, row/column runs and transparent or palette pixel formats.
enum BdImagePixelEncoding : u32 {
  kBdImageRawPixels = 1,
  kBdImageRowRuns = 2,
  kBdImageColumnRuns = 4,
  kBdImageTransparentRows = 8,
  kBdImageTransparentColumns = 16,
  kBdImagePaletteRows = 128,
  kBdImagePaletteColumns = 0x100,
};

// Grid archives write this separator between the column map, cell data and material sections.
enum BdGridArchiveMarker : u32 {
  kBdGridArchiveSectionMarker = 0xeaeaeaea,
};

static u32 BdArchiveTextPage() {
  return ScInstalledWesternTextPage(BdGetLanguageManager()->GetCurrentLanguage());
}

// Load asset geometry and metadata, then retain the separately stored compressed image.
// Decode the displayed name at the legacy text boundary and reject incomplete scalar metadata.
u8 BdIxf::ReadAsset(const RsKey *key, BdAsset **out, u32) {
  u32 page = BdArchiveTextPage();
  if (!page) {
    *out = nullptr;
    return 0;
  }
  BdAsset *asset = new BdAsset;
  RsKey image_key(kBdAssetImageRecordType, key->group, key->instance);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoOpenRecord(*key, &record);
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u8 version = 0;
      BdRect icon = {}, preview = {};
      u32 identity = 0, flags = 0, kind = 255;
      f32 height = 0, width = 0, inverse_height = 0, inverse_width = 0;
      ScString name;
      bool read = stream->ReadU8(&version) && stream->ReadI32(&icon.left) && stream->ReadI32(&icon.top) &&
          stream->ReadI32(&icon.right) && stream->ReadI32(&icon.bottom) && stream->ReadI32(&preview.left) &&
          stream->ReadI32(&preview.top) && stream->ReadI32(&preview.right) && stream->ReadI32(&preview.bottom) &&
          stream->ReadU32(&identity) && stream->ReadU32(&flags) && stream->ReadF32(&height) &&
          stream->ReadF32(&width) && stream->ReadF32(&inverse_height) && stream->ReadF32(&inverse_width) &&
          RsReadLegacyText(stream, &name, page) &&
          stream->ReadU32(&image_key.type) && stream->ReadU32(&image_key.group) && stream->ReadU32(&image_key.instance);
      if (read && version >= 10)
        read = stream->ReadU32(&kind);
      stream->Release();
      if (!read) {
        DoCloseRecord(&record);
        *out = nullptr;
        return 0;
      }
      asset->icon_bounds = icon;
      asset->preview_bounds = preview;
      asset->definition_id = version < 10 ? 0 : identity;
      asset->kind = static_cast<i8>(version < 10 ? identity >> 24 : kind);
      asset->flags = flags & ~8u;
      asset->height = height;
      asset->width = width;
      asset->inverse_height = inverse_height;
      asset->inverse_width = inverse_width;
      const char *separator = name.Strlen() >= 10 ? strstr(name.Data(), " |") : nullptr;
      unsigned parsed;
      if (separator && separator != name.Data() && sscanf(separator + 2, "%8x", &parsed) == 1)
        asset->name.FromChar(name.Data(), static_cast<u32>(separator - name.Data()));
      else
        asset->name = name;
      result = 1;
    }
    DoCloseRecord(&record);
  }
  *out = nullptr;
  if (result) {
    GxBuffer *image = nullptr;
    if (ReadStoredImage(&image_key, &image) && image) {
      if (asset->flags & 2)
        image->SetTransparency(kBdStoredImageTransparencyColor);
      asset->SetImage(image);
      *out = asset;
      image->Release();
    } else {
      result = 0;
    }
  }
  return result;
}

// Write the asset's geometry, flags, dimensions and name before serializing its optional retained image.
// Encode the displayed name before writing; retain the original metadata/image I/O result ordering.
u8 BdIxf::WriteAsset(const RsKey *key, BdAsset *value) {
  if (!value)
    return 0;
  const ScString *name = &value->name;
  ScString encoded_name;
  if (!ScEncodeLegacyText(BdArchiveTextPage(), *name, &encoded_name))
    return 0;
  name = &encoded_name;
  RsKey image_key(kBdAssetImageRecordType, key->group, key->instance);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoCreateNewRecord(*key, &record);
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU8(10);
      stream->WriteI32(value->icon_bounds.left);
      stream->WriteI32(value->icon_bounds.top);
      stream->WriteI32(value->icon_bounds.right);
      stream->WriteI32(value->icon_bounds.bottom);
      stream->WriteI32(value->preview_bounds.left);
      stream->WriteI32(value->preview_bounds.top);
      stream->WriteI32(value->preview_bounds.right);
      stream->WriteI32(value->preview_bounds.bottom);
      stream->WriteU32(value->definition_id);
      stream->WriteU32(value->flags & ~8u);
      stream->WriteF32(value->height);
      stream->WriteF32(value->width);
      stream->WriteF32(value->inverse_height);
      stream->WriteF32(value->inverse_width);
      stream->WriteString(name);
      stream->WriteU32(image_key.type);
      stream->WriteU32(image_key.group);
      stream->WriteU32(image_key.instance);
      stream->WriteU32(static_cast<u8>(value->kind));
      stream->Release();
      result = 1;
    }
    DoCloseRecord(&record);
    if (result) {
      GxBuffer *image = value->RenderImage(nullptr, kBdAssetWhiteRenderBackground);
      if (image) {
        result = WriteImage(&image_key, image);
        image->Release();
      }
    }
  }
  return result;
}

BdImageArchive::BdImageArchive()
    : RsIndexedFile(nullptr), field_0ec(0), field_0f0(0), color_key(0), field_0f8(UINT32_MAX), field_0fc(0),
      field_0fd(1), field_100(0) {
}

// Destroy the indexed-file base and its owned file, index and record in their native C++ lifetime order.
BdImageArchive::~BdImageArchive() = default;

BdArchive::BdArchive() : archive_open(0) {
}

BdArchive::~BdArchive() = default;

BdIxf::BdIxf() : catalog(nullptr) {
}

// Destroy the intermediate archive after the building archive's borrowed catalog leaves scope.
BdIxf::~BdIxf() = default;

// Replace the borrowed catalog without retaining or releasing either pointer.
void BdIxf::SetCatalog(BdCatalog *value) {
  if (catalog)
    catalog = nullptr;
  catalog = value;
}

u8 BdIxf::ReadPreviewImage(const RsKey *key, GxBuffer **out) {
  return ReadStoredImage(key, out);
}

u8 BdIxf::WritePreviewImage(const RsKey *key, GxBuffer *image) {
  return WriteImage(key, image);
}

bool BdImageArchive::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpImageSource, SpImageSource);
  return RsIndexedFile::QueryInterface(iid, out);
}

u32 BdImageArchive::AddRef() {
  return RsIndexedFile::AddRef();
}

u32 BdImageArchive::Release() {
  return RsIndexedFile::Release();
}

// Initialize the local archive's magenta key and target format from the architect's graphics service.
// Both lookups occur before the original indexed-file initialization.
bool BdImageArchive::Init(u32 id, const ScStringView *name, bool resolved) {
  BdGetGraphics()->ConvertRGBValueToNative16Bit(&color_key, 255, 0, 255);
  target_format = static_cast<i16>(BdGetGraphics()->GetScreenColorType().color);
  return RsIndexedFile::Init(id, name, resolved);
}

bool BdImageArchive::Shutdown() {
  return RsIndexedFile::Shutdown();
}

// Read the eight-byte image rectangle from the original record-data interface and assign the key fields.
u8 BdImageArchive::ReadMetadata(SpImage *out, const RsKey *key) {
  RsRecord *record = nullptr;
  if (!DoOpenRecord(*key, &record))
    return 0;
  RsRecordData *data;
  if (record->QueryInterface(IID_RsRecordData, reinterpret_cast<void **>(&data))) {
    u32 size;
    i16 rectangle[4] = {};
    data->GetSize(&size);
    data->ReadBlock(rectangle, sizeof(rectangle));
    data->Release();
    out->references = 0;
    out->resource = key->instance;
    out->group = key->group;
    out->left = rectangle[0];
    out->buffer = nullptr;
    out->top = rectangle[1];
    out->right = rectangle[2];
    out->bottom = rectangle[3];
    DoCloseRecord(&record);
    return 1;
  }
  DoCloseRecord(&record);
  return 0;
}

// Copy one indexed resource record in the source's 6144-byte chunks, preserving record and stream release order.
u8 BdCopyArchiveRecord(
    BdArchive *source, const RsKey *source_key, BdArchive *destination, const RsKey *destination_key) {
  RsRecord *input_record = nullptr, *output_record = nullptr;
  RsStream *input = nullptr, *output = nullptr;
  u8 opened_input = source->DoOpenRecord(*source_key, &input_record);
  u8 opened_output = destination->DoCreateNewRecord(*destination_key, &output_record);
  u8 result = 0;
  if (opened_input && opened_output) {
    bool input_stream = input_record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&input));
    bool output_stream = output_record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&output));
    if (input_stream && output_stream) {
      u32 size = 0;
      input->GetSize(&size);
      i32 remaining = static_cast<i32>(size);
      u8 *bytes = new u8[6144]();
      result = 1;
      while (remaining > 0) {
        u32 count = remaining >= 6144 ? 6144 : static_cast<u32>(remaining);
        input->ReadBlock(bytes, count);
        output->WriteBlock(bytes, count);
        remaining -= static_cast<i32>(count);
      }
      delete[] bytes;
    }
    if (input_stream)
      input->Release();
    if (output_stream)
      output->Release();
  }
  if (opened_input)
    source->DoCloseRecord(&input_record);
  if (opened_output)
    destination->DoCloseRecord(&output_record);
  return result;
}

// Allocate and retain a definition, then read its name, three variants and three dimensions from the archive record.
// Decode names at their boundary while retaining ignored I/O failures and the original close/publication order.
u8 BdIxf::ReadDefinition(const RsKey *key, BdDefinition **out) {
  if (!out)
    return 0;
  *out = nullptr;
  u32 page = BdArchiveTextPage();
  if (!page)
    return 0;
  BdDefinition *definition = new BdDefinition;
  ++definition->references;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoOpenRecord(*key, &record);
  if (result && record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    u8 version;
    stream->ReadU8(&version);
    bool converted, valid = true;
    RsReadLegacyText(stream, &definition->name, page, &converted);
    valid &= converted;
    for (u32 i = 0; i < 3; ++i) {
      RsReadLegacyText(stream, &definition->variants[i], page, &converted);
      valid &= converted;
    }
    stream->ReadU32(reinterpret_cast<u32 *>(&definition->size.x));
    stream->ReadU32(reinterpret_cast<u32 *>(&definition->size.y));
    stream->ReadU32(reinterpret_cast<u32 *>(&definition->size.z));
    stream->Release();
    result = valid;
  }
  DoCloseRecord(&record);
  *out = definition;
  return result;
}

// Write the definition version, name, three variants and three dimensions to a newly created record.
// Encode labels before writing; keep ignored field I/O failures and the original open/query/close result.
u8 BdIxf::WriteDefinition(const RsKey *key, const BdDefinition *value) {
  if (!value)
    return 0;
  const ScString *name = &value->name, *variants = value->variants;
  u32 page = BdArchiveTextPage();
  ScString encoded_name, encoded_variants[3];
  if (!ScEncodeLegacyText(page, *name, &encoded_name))
    return 0;
  for (u32 i = 0; i < 3; ++i)
    if (!ScEncodeLegacyText(page, variants[i], &encoded_variants[i]))
      return 0;
  name = &encoded_name;
  variants = encoded_variants;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoCreateNewRecord(*key, &record);
  if (result && record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    stream->WriteU8(10);
    stream->WriteString(name);
    for (u32 i = 0; i < 3; ++i)
      stream->WriteString(variants + i);
    stream->WriteU32(static_cast<u32>(value->size.x));
    stream->WriteU32(static_cast<u32>(value->size.y));
    stream->WriteU32(static_cast<u32>(value->size.z));
    stream->Release();
    result = 1;
  }
  DoCloseRecord(&record);
  return result;
}

// Open a grid-info record and read its linked resource key after the leading view byte.
// Keep the open result when the stream query fails, and close every successfully opened record.
u8 BdIxf::ReadGridInfoKey(const RsKey *key, RsKey *out) {
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoOpenRecord(*key, &record);
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u8 view;
      stream->ReadU8(&view);
      stream->ReadKey(out);
      stream->Release();
      result = 1;
    }
    DoCloseRecord(&record);
  }
  return result;
}

// Write the grid's view, linked-resource key, descriptive strings and identifier into its metadata record.
// Encode displayed text using its evidenced font context, leaving the transformed registration serial raw.
u8 BdIxf::WriteGridInfo(u32 group, u32 instance, const BdGrid *grid) {
  const ScString *name = &grid->localized_466, *architect = &grid->localized_465, *description = &grid->field_038;
  u32 page = BdArchiveTextPage();
  ScString encoded_name, encoded_architect, encoded_description;
  if (!ScEncodeLegacyText(page, *name, &encoded_name) ||
      !ScEncodeLegacyText(page, *architect, &encoded_architect) ||
      !ScEncodeLegacyText(page, *description, &encoded_description))
    return 0;
  name = &encoded_name;
  architect = &encoded_architect;
  description = &encoded_description;
  RsKey metadata(kRsBuildingOverrideResourceType, group, instance), linked(0x6300, group, instance);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoCreateNewRecord(metadata, &record);
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU8(grid->view_mode);
      stream->WriteKey(&linked);
      stream->WriteString(name);
      stream->WriteString(architect);
      stream->WriteString(description);
      stream->WriteString(&grid->field_194);
      stream->WriteU32(grid->field_1a8);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  return result;
}

static RsKey BdDocumentKey(const RsKey *source, u32 type, u32 default_instance) {
  return RsKey(
      type, source->group, source->instance == kBdDocumentRecordInstance ? default_instance : source->instance);
}

// Resolve the actual stored game-language identifier, including the protocol's explicit current-language alias.
static u32 BdDocumentTextPage(u32 language) {
  if (language == kScLanguageCurrent)
    language = BdGetLanguageManager()->GetCurrentLanguage();
  return ScLanguageCrtCodePage(language);
}

// The document carries its own language; registration and identifier fields are outside this conversion.
bool BdCanEncodeDocumentText(const BdGrid *grid) {
  u32 page = BdDocumentTextPage(grid->field_054);
  ScString encoded;
  return ScEncodeLegacyText(page, grid->localized_466, &encoded) &&
      ScEncodeLegacyText(page, grid->localized_465, &encoded) &&
      ScEncodeLegacyText(page, grid->field_038, &encoded);
}

// Read primary and auxiliary records in order, decoding only assigned displayed text once its stored language is known.
// Preserve untouched fields on incomplete reads and retain the registration bytes without interpreting them as text.
u8 BdIxf::ReadDocument(const RsKey *key, BdDocument *out) {
  [[maybe_unused]] bool read_first = false, read_second = false, read_description = false;
  ScString *first = &out->text_008, *second = &out->text_01c, *description = &out->text_030;
  ScString raw_first, raw_second, raw_description;
  first = &raw_first;
  second = &raw_second;
  description = &raw_description;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoOpenRecord(*key, &record), current;
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u8 length;
      if (stream->ReadU8(&out->byte_004) && stream->ReadU8(&length)) {
        u8 *bytes = new u8[static_cast<u32>(length) + 1];
        if (stream->ReadBlock(bytes, length)) {
          first->FromChar(reinterpret_cast<const char *>(bytes), length);
          read_first = true;
        }
        delete[] bytes;
        if (stream->ReadU8(&length)) {
          bytes = new u8[static_cast<u32>(length) + 1];
          if (stream->ReadBlock(bytes, length)) {
            second->FromChar(reinterpret_cast<const char *>(bytes), length);
            read_second = true;
          }
          delete[] bytes;
        }
      }
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  RsKey part = BdDocumentKey(key, kBdDocumentDescriptionRecordType, kBdDocumentDescriptionDefaultInstance);
  current = DoOpenRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u8 length;
      if (stream->ReadU8(&length) && length) {
        u8 *bytes = new u8[length];
        if (stream->ReadBlock(bytes, length)) {
          description->FromChar(reinterpret_cast<const char *>(bytes), length);
          read_description = true;
        }
        delete[] bytes;
      }
      stream->Release();
    }
    DoCloseRecord(&record);
  } else {
    out->text_030.FromChar("");
  }
  part = BdDocumentKey(key, kBdDocumentBuildingTypeRecordType, kBdDocumentBuildingTypeDefaultInstance);
  current = DoOpenRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u32 value;
      out->word_044 = stream->ReadU32(&value) ? value : 0;
      stream->Release();
    }
    DoCloseRecord(&record);
  } else {
    out->word_044 = 0;
  }
  part = BdDocumentKey(key, kBdDocumentLanguageRecordType, kBdDocumentLanguageDefaultInstance);
  current = DoOpenRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u32 value;
      out->word_04c = stream->ReadU32(&value) ? value : BdGetLanguageManager()->GetCurrentLanguage();
      stream->Release();
    }
    DoCloseRecord(&record);
  } else {
    out->word_04c = BdGetLanguageManager()->GetCurrentLanguage();
  }
  // The existing auxiliary record establishes the page; preserve fields that no completed read assigned.
  u32 page = BdDocumentTextPage(out->word_04c);
  if ((read_first && !ScDecodeLegacyText(page, raw_first, &out->text_008)) ||
      (read_second && !ScDecodeLegacyText(page, raw_second, &out->text_01c)) ||
      (read_description && !ScDecodeLegacyText(page, raw_description, &out->text_030)))
    return 0;
  part = BdDocumentKey(key, kBdDocumentModeRecordType, kBdDocumentModeDefaultInstance);
  current = DoOpenRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      const u8 defaults[4] = {2, 0, 28, 0};
      for (u32 i = 0; i < 4; ++i)
        if (!stream->ReadU8(&out->bytes_048[i]))
          out->bytes_048[i] = defaults[i];
      stream->Release();
    }
    DoCloseRecord(&record);
  } else {
    out->bytes_048[0] = 1;
    out->bytes_048[1] = out->bytes_048[2] = out->bytes_048[3] = 0;
  }
  part = RsKey(kBdDocumentRegistrationRecordType, key->group, key->instance);
  current = DoOpenRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->ReadString(&out->text_058);
      stream->ReadU32(&out->word_054);
      stream->Release();
    }
    DoCloseRecord(&record);
  } else {
    out->text_058.FromChar("");
    out->word_054 = 0;
  }
  return result;
}

// Encode displayed text under the stored document language while writing records in their original order.
// Close every created record; unrepresentable UTF-8 reports a recoverable failure and registration bytes stay raw.
u8 BdIxf::WriteDocument(const RsKey *key, const BdDocument *value) {
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = DoCreateNewRecord(*key, &record), current;
  if (result) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      const ScString *first_text = &value->text_008, *second_text = &value->text_01c;
      ScString encoded_first, encoded_second;
      u32 page = BdDocumentTextPage(value->word_04c);
      if (!ScEncodeLegacyText(page, *first_text, &encoded_first) ||
          !ScEncodeLegacyText(page, *second_text, &encoded_second)) {
        stream->Release();
        DoCloseRecord(&record);
        return 0;
      }
      first_text = &encoded_first;
      second_text = &encoded_second;
      u8 first_length = static_cast<u8>(first_text->Strlen());
      u8 second_length = static_cast<u8>(second_text->Strlen());
      const char *first = first_text->Data(), *second = second_text->Data();
      stream->WriteU8(value->byte_004);
      stream->WriteU8(first_length);
      stream->WriteBlock(first, first_length);
      stream->WriteU8(second_length);
      stream->WriteBlock(second, second_length);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  RsKey part = BdDocumentKey(key, kBdDocumentDescriptionRecordType, kBdDocumentDescriptionDefaultInstance);
  current = DoCreateNewRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      const ScString *description = &value->text_030;
      ScString encoded_description;
      if (!ScEncodeLegacyText(BdDocumentTextPage(value->word_04c), *description, &encoded_description)) {
        stream->Release();
        DoCloseRecord(&record);
        return 0;
      }
      description = &encoded_description;
      u8 length = static_cast<u8>(description->Strlen());
      const char *text = description->Data();
      if (stream->WriteU8(length))
        stream->WriteBlock(text, length);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  part = BdDocumentKey(key, kBdDocumentBuildingTypeRecordType, kBdDocumentBuildingTypeDefaultInstance);
  current = DoCreateNewRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU32(value->word_044);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  part = BdDocumentKey(key, kBdDocumentLanguageRecordType, kBdDocumentLanguageDefaultInstance);
  current = DoCreateNewRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU32(value->word_04c);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  part = BdDocumentKey(key, kBdDocumentModeRecordType, kBdDocumentModeDefaultInstance);
  current = DoCreateNewRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      for (u32 i = 0; i < 4; ++i)
        stream->WriteU8(value->bytes_048[i]);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  part = RsKey(kBdDocumentRegistrationRecordType, key->group, key->instance);
  current = DoCreateNewRecord(part, &record);
  result |= current;
  if (current) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteString(&value->text_058);
      stream->WriteU32(value->word_054);
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  return result;
}

// Callback results are not consumed; the enumeration methods always return the original success byte.
static void BdVisitArchiveKeys(BdIxf *archive, u32 type, BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  RsKeyList keys;
  archive->GetResourceKeyList(&keys);
  u32 count = keys.Size();
  for (u32 i = 0; i < count; ++i) {
    RsKey *key = keys.GetKey(i);
    if (key->type == type)
      visitor(key, context);
  }
}

u8 BdIxf::EnumGrids(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdGridRecordType, visitor, context);
  return 1;
}

u8 BdIxf::EnumPreviews(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdPreviewRecordType, visitor, context);
  return 1;
}

u8 BdIxf::EnumAssets(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdAssetRecordType, visitor, context);
  return 1;
}

u8 BdIxf::EnumDocuments(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdDocumentRecordType, visitor, context);
  return 1;
}

u8 BdIxf::EnumStoredImages(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdAssetImageRecordType, visitor, context);
  return 1;
}

u8 BdIxf::EnumGridInfo(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kRsBuildingOverrideResourceType, visitor, context);
  return 1;
}

u8 BdIxf::EnumDefinitions(BdArchiveKeyVisitor visitor, BdArchiveKeyContext *context) {
  BdVisitArchiveKeys(this, kBdObjectDefinitionRecordType, visitor, context);
  return 1;
}

// Write the installed BIN directory header and its twenty frame records in source order.
static void BdWriteFrameDirectory(RsStream *stream, const BdFrameDirectory *directory) {
  stream->WriteU8('B');
  stream->WriteU8('I');
  stream->WriteU8('N');
  stream->WriteU8(13);
  stream->WriteU32(1);
  stream->WriteU32(29440);
  stream->WriteU8(5);
  stream->WriteU8(4);
  stream->WriteU8(4);
  stream->WriteU8(20);
  stream->WriteU8(1);
  stream->WriteU8(0);
  stream->WriteU32(0);
  stream->WriteU16(20);
  for (u32 i = 0; i < 20; ++i) {
    const BdFrameDirectoryEntry *entry = &directory->entries[i];
    stream->WriteI16(static_cast<i16>(i));
    stream->WriteU32(entry->group);
    stream->WriteU32(entry->instance);
    stream->WriteI8(entry->field_01);
    stream->WriteI16(entry->field_02);
    stream->WriteI16(entry->field_04);
  }
}

// Write one sprite-frame directory record and close the archive record after the stream operations.
// As in the installed method, failure to create or query the record does not change its success byte.
u8 BdArchive::WriteDirectory(const RsKey *key, const BdFrameDirectory *directory) {
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (DoCreateNewRecord(*key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)))
      BdWriteFrameDirectory(stream, directory);
    DoCloseRecord(&record);
  }
  return 1;
}

// Write a sprite-frame directory and its trailing empty BIN marker, then close the created record.
u8 BdArchive::WriteDirectoryWithTerminator(const RsKey *key, const BdFrameDirectory *directory) {
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (DoCreateNewRecord(*key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      BdWriteFrameDirectory(stream, directory);
      stream->WriteU8('B');
      stream->WriteU8('I');
      stream->WriteU8('N');
      stream->WriteU8(13);
      stream->WriteU32(0);
      stream->WriteU32(0);
    }
    DoCloseRecord(&record);
  }
  return 1;
}

// Read twenty directory entries and append their rectangle and image keys to the supplied original vector outputs.
// Failed record or stream queries leave the vectors unchanged and retain the original success byte.
u8 BdArchive::ReadDirectoryKeys(const RsKey *key, std::vector<RsKey> *rectangles, std::vector<RsKey> *images) {
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u32 group = 0, instance = 0;
  if (DoOpenRecord(*key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->SkipBytes(24);
      for (u32 i = 0; i < 20; ++i) {
        stream->SkipBytes(2);
        stream->ReadU32(&group);
        stream->ReadU32(&instance);
        stream->SkipBytes(5);
        rectangles->push_back(RsKey(1, group, instance));
        images->push_back(RsKey(0, group, instance));
      }
    }
    DoCloseRecord(&record);
  }
  return 1;
}

static void BdInsertGridAssetId(u32 *ids, u32 *count, u32 id) {
  u32 index = 0;
  while (index < *count && ids[index] < id)
    ++index;
  if (index < *count && ids[index] == id)
    return;
  for (u32 i = *count; i > index; --i)
    ids[i] = ids[i - 1];
  ids[index] = id;
  ++*count;
}

// Check the labels of the exact material/dependency records selected by WriteGrid without changing grid usage counts.
// This preflight separates unsupported UTF-8 text from original stream failures that SaveGrid deliberately ignores.
bool BdCanEncodeGridText(const BdGrid *grid, BdCatalog *catalog) {
  u32 page = BdArchiveTextPage(), uses[256] = {}, ids[1024], id_count = 0;
  if (!page)
    return false;
  i32 end_x = grid->bounds.origin.x + grid->bounds.size.x, end_y = grid->bounds.origin.y + grid->bounds.size.y;
  for (i32 x = grid->bounds.origin.x; x < end_x; ++x)
    for (i32 y = grid->bounds.origin.y; y < end_y; ++y)
      for (u32 index = grid->columns[x][y]; index; index = grid->cells[index].link_height_kind & kBdCellNextMask)
        for (u32 i = 0; i < 6; ++i) {
          ++uses[grid->cells[index].parts[i].first];
          ++uses[grid->cells[index].parts[i].second];
        }
  ScString encoded;
  for (u32 i = 0; i < 256; ++i) {
    BdAsset *asset = grid->materials->slots[i].value;
    if (!uses[i] || !asset)
      continue;
    if (!ScEncodeLegacyText(page, asset->name, &encoded))
      return false;
    if (asset->IsKind6Or7Or16())
      BdInsertGridAssetId(ids, &id_count, asset->definition_id);
    if (asset->IsKind6Or7Or16() && asset->IsKind8Through16()) {
      BdDefinition *definition = nullptr;
      if (catalog->GetDefinition(asset->definition_id, &definition)) {
        for (u32 j = 0; j < 3; ++j)
          BdInsertGridAssetId(ids, &id_count, definition->variant_ids[j]);
        definition->Release();
      }
    }
  }
  for (u32 i = 0; i < id_count; ++i) {
    BdAsset *asset = nullptr;
    if (!catalog->GetAsset(ids[i], &asset))
      continue;
    if (!ScEncodeLegacyText(page, asset->name, &encoded))
      return false;
    if (asset->IsKind8Through16()) {
      BdDefinition *definition = nullptr;
      if (catalog->GetDefinition(asset->definition_id, &definition)) {
        bool valid = ScEncodeLegacyText(page, definition->name, &encoded);
        for (u32 j = 0; j < 3; ++j)
          valid &= ScEncodeLegacyText(page, definition->variants[j], &encoded);
        definition->Release();
        if (!valid)
          return false;
      }
    }
  }
  return true;
}

// Write the grid's cell pool, material names and dependent asset and definition records.
// The compression boundary excludes cases where the original source or destination buffers would be overrun.
u8 BdIxf::WriteGrid(const RsKey *key, BdGrid *grid) {
  if (!BdCanEncodeGridText(grid, catalog))
    return 0;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!DoCreateNewRecord(*key, &record))
    return 0;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    DoCloseRecord(&record);
    return 0;
  }
  BdMaterials *materials = nullptr;
  grid->GetMaterials(&materials);
  stream->WriteU8(8);
  u32 limit = grid->side == 1 ? 28 : grid->side == 2 ? 64 : grid->side == 3 ? 108 :
      grid->side == 4 || grid->side == 5 ? 144 : 0;
  if (limit && static_cast<u32>(grid->height) > limit)
    grid->height = static_cast<i32>(limit);
  stream->WriteU8(grid->rotation);
  stream->WriteU8(static_cast<u8>(grid->side));
  stream->WriteU8(grid->view_mode);
  stream->WriteU8(234);
  stream->WriteU16(static_cast<u16>(grid->height));
  stream->WriteU16(60138);
  stream->WriteU32(grid->cell_count);
  stream->WriteU32(kBdGridArchiveSectionMarker);
  stream->WriteU32(4);
  stream->WriteU32(32);
  stream->WriteU32(kBdGridArchiveSectionMarker);
  u32 side = static_cast<u32>(grid->side);
  stream->WriteU32(4u * side * side);
  for (u32 i = 0; i < side; ++i)
    stream->WriteBlock(grid->columns[i], 4u * side);
  stream->WriteU32(kBdGridArchiveSectionMarker);
  u32 capacity = archive_codec.CompressCapacity(32u * grid->cell_count);
  if (capacity > kBdGridCompressedCapacityLimit) {
    stream->Release();
    DoCloseRecord(&record);
    materials->Release();
    return 0;
  }
  u8 *compressed = new u8[static_cast<usize>(capacity) + capacity / 4 + 64];
  u32 written = 0;
  bool encoded = archive_codec.Compress(reinterpret_cast<const u8 *>(grid->cells), static_cast<i32>(capacity),
      compressed, &written);
  if (written > capacity) {
    delete[] compressed;
    stream->Release();
    DoCloseRecord(&record);
    materials->Release();
    return 0;
  }
  if (encoded) {
    stream->WriteU32(written);
    stream->WriteBlock(compressed, written);
  }
  delete[] compressed;
  stream->WriteU32(kBdGridArchiveSectionMarker);
  grid->RecountMaterials();
  u32 last = 0, ids[1024], id_count = 0;
  for (u32 i = 1; i < 256; ++i) {
    BdAsset *asset = nullptr;
    if (materials->Get(i, &asset))
      last = i;
  }
  stream->WriteU32(last + 1);
  for (u32 i = 0; i <= last; ++i) {
    BdAsset *asset = nullptr;
    u32 uses = 0, id = 0;
    ScString name;
    materials->GetUse(i, &uses);
    if (materials->Get(i, &asset)) {
      id = asset->definition_id;
      name = asset->name;
      if (asset->IsKind6Or7Or16() && uses)
        BdInsertGridAssetId(ids, &id_count, id);
    }
    ScString encoded_name;
    if (!ScEncodeLegacyText(BdArchiveTextPage(), name, &encoded_name)) {
      stream->Release();
      DoCloseRecord(&record);
      materials->Release();
      return 0;
    }
    stream->WriteString(&encoded_name);
    stream->WriteU32(id);
  }
  stream->WriteU32(kBdGridArchiveSectionMarker);
  stream->Release();
  DoCloseRecord(&record);
  grid->ResetChanges();
  grid->changed = 0;
  for (u32 i = 0; i < 256; ++i) {
    BdAsset *asset = nullptr;
    if (materials->Get(i, &asset) && asset->IsKind6Or7Or16() && asset->IsKind8Through16()) {
      BdDefinition *definition = nullptr;
      if (catalog->GetDefinition(asset->definition_id, &definition)) {
        for (u32 j = 0; j < 3; ++j)
          BdInsertGridAssetId(ids, &id_count, definition->variant_ids[j]);
        definition->Release();
      }
    }
  }
  materials->Release();
  u8 result = 1;
  for (u32 i = 0; i < id_count; ++i) {
    BdAsset *asset = nullptr;
    if (catalog->GetAsset(ids[i], &asset)) {
      RsKey asset_key(kBdAssetRecordType, kBdBuildingArchiveGroup, i);
      WriteAsset(&asset_key, asset);
      if (asset->IsKind8Through16()) {
        BdDefinition *definition = nullptr;
        if (catalog->GetDefinition(asset->definition_id, &definition)) {
          RsKey definition_key(kBdObjectDefinitionRecordType, kBdBuildingArchiveGroup, i);
          result = WriteDefinition(&definition_key, definition);
          definition->Release();
        }
      }
    }
  }
  return result;
}

// Encode a supported graphics buffer into an indexed image record, converting format 5 through a format 7 buffer.
// Preserve the original lock, record and converted-image ownership order, including early-return allocation leaks.
u8 BdArchive::WriteImage(const RsKey *key, GxBuffer *image) {
  GxBuffer *converted = nullptr, *source = image;
  i32 width = source->Width(), height = source->Height();
  i32 format = static_cast<i32>(source->GetColorType().color);
  u32 byte_count;
  u8 kind;
  if (format == 7) {
    kind = 240;
    byte_count = 2u * static_cast<u32>(width) * static_cast<u32>(height);
  } else if (format == 5) {
    kind = 240;
    byte_count = 2u * static_cast<u32>(width) * static_cast<u32>(height);
    BdGetGraphics()->CreateBuffer(&converted);
    i32 converted_height = source->Height(), converted_width = source->Width();
    converted->Init(converted_width, converted_height, GxColorType(static_cast<GxColorType::Color>(7)), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, true);
    if (converted->Lock(GxLockFlags::Fill)) {
      converted->Fill(0x7c1f, nullptr);
      converted->Unlock(GxLockFlags::Fill);
    }
    converted->SetTransparency(0x7c1f);
    converted->CopyBuffer(source);
    converted->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>(5), static_cast<GxColorType::Color>(7));
    source = converted;
    format = static_cast<i32>(source->GetColorType().color);
  } else if (format == 14) {
    kind = 241;
    byte_count = 4u * static_cast<u32>(width) * static_cast<u32>(height);
  } else {
    return 0;
  }
  u32 capacity = archive_codec.CompressCapacity(byte_count);
  u8 *compressed = new u8[capacity];
  u8 result = source->Lock(GxLockFlags::Bits);
  if (!result)
    return 0;
  u32 written;
  bool encoded = archive_codec.Compress(
      static_cast<u8 *>(source->GetColorSurfaceBits()), static_cast<i32>(byte_count), compressed, &written);
  source->Unlock(GxLockFlags::Bits);
  if (!encoded)
    return 0;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (DoCreateNewRecord(*key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU8(kind);
      stream->WriteU8(static_cast<u8>(format));
      stream->WriteU16(static_cast<u16>(width));
      stream->WriteU16(static_cast<u16>(height));
      stream->WriteBlock(compressed, written);
      stream->Release();
      result = 1;
    }
    DoCloseRecord(&record);
  }
  if (converted)
    converted->Release();
  delete[] compressed;
  return result;
}

// Read the architect's compressed image record and restore its graphics buffer and color key.
// Validate external record and QFS lengths before entering the original decoder's unbounded packet loop.
u8 BdArchive::ReadStoredImage(const RsKey *key, GxBuffer **out) {
  i32 display_format = static_cast<i32>(BdGetGraphics()->GetScreenColorType().color);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  // These two stock exits return the untouched high byte of the x86 key-pointer argument slot.
  if (!DoOpenRecord(*key, &record))
    return static_cast<u8>(reinterpret_cast<uptr>(key) >> 24);
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    DoCloseRecord(&record);
    return static_cast<u8>(reinterpret_cast<uptr>(key) >> 24);
  }
  u32 record_size = 0;
  u8 kind = 0, stored_format = 0;
  u16 width = 0, height = 0;
  bool read = stream->GetSize(&record_size) && record_size >= 6 && stream->ReadU8(&kind) &&
      stream->ReadU8(&stored_format) && stream->ReadU16(&width) && stream->ReadU16(&height);
  u32 bytes = kind == 240 ? 2u * width * height : kind == 241 ? 4u * width * height : 0;
  u32 payload_size = read ? record_size - 6 : 0;
  if (!read || !bytes) {
    stream->Release();
    DoCloseRecord(&record);
    return 0;
  }
  if (payload_size > bytes)
    payload_size = bytes;
  u8 *payload = new u8[payload_size];
  read = stream->ReadBlock(payload, payload_size);
  stream->Release();
  u32 decoded_size = 0;
  if (!read || !BdValidateQfsInput(payload, payload_size, &decoded_size) || decoded_size != bytes) {
    delete[] payload;
    DoCloseRecord(&record);
    return 0;
  }
  GxBuffer *image = nullptr;
  BdGetGraphics()->CreateBuffer(&image);
  u8 result = image->Init(width, height, GxColorType(static_cast<GxColorType::Color>(kind == 240 ? 7 : 14)), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, true);
  if (kind == 240 && result)
    image->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
  if (result) {
    u32 consumed = archive_codec.CompressedSize(payload);
    result = image->Lock(GxLockFlags::Bits);
    if (result) {
      result = archive_codec.Decompress(payload, bytes, static_cast<u8 *>(image->GetColorSurfaceBits()), &consumed);
      image->Unlock(GxLockFlags::Bits);
      if (result && kind == 240 && display_format == 5) {
        GxBuffer *converted = nullptr;
        BdGetGraphics()->CreateBuffer(&converted);
        i32 converted_width = image->Width(), converted_height = image->Height();
        converted->Init(converted_width, converted_height, GxColorType(static_cast<GxColorType::Color>(5)), nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, true);
        converted->CopyBuffer(image);
        converted->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
        converted->Convert16BitBufferFromFormatToFormat(
            static_cast<GxColorType::Color>(7), static_cast<GxColorType::Color>(5));
        image->Release();
        image = converted;
      }
    }
  }
  if (!result) {
    image->Shutdown();
    image->Release();
  } else {
    *out = image;
  }
  delete[] payload;
  DoCloseRecord(&record);
  return result;
}

u8 BdArchive::OpenArchive(const ScStringView *name) {
  if (!BdFileExists(name) || !static_cast<RsIndexedAccess *>(this)->OpenRead(*name))
    return 0;
  archive_open = 1;
  return 1;
}

// Remove an existing target before opening a fresh writable indexed archive.
// The original tests the deletion result's low byte and sets the open flag only on success.
u8 BdArchive::CreateArchive(const ScStringView *name) {
  if ((BdFileExists(name) && !static_cast<u8>(BdDeleteFile(name))) ||
      !static_cast<RsIndexedAccess *>(this)->OpenWrite(*name))
    return 0;
  archive_open = 1;
  return 1;
}

// Shut down the archive through its primary virtual interface before clearing the intermediate open flag.
u8 BdArchive::CloseArchive() {
  if (!Shutdown())
    return 0;
  archive_open = 0;
  return 1;
}

// Select the original pixel decoder without normalizing its byte result. The two legacy format stubs really
// return zero, while unrecognized flags return their low byte without accessing the source or target.
u8 BdImageArchive::DecodePixels(GxBuffer *target, const u8 *source, u32 payload_size, u32 flags, i32 source_format) {
  if (flags & kBdImageIndexedFormatFlag) {
    if (flags & 0x8000)
      return 0;
    if (flags & 0x80000)
      return DecodePlainBitmap(target, source, payload_size);
  } else {
    switch (flags) {
    case kBdImageRawPixels:
      return BdDecodeRaw(target, source, payload_size, source_format);
    case kBdImageRowRuns:
      return BdDecodeRowRuns(target, source, payload_size, source_format);
    case kBdImageColumnRuns:
      return BdDecodeColumnRuns(target, source, payload_size, source_format);
    case kBdImageTransparentRows:
      return DecodeTransparentRows(target, source, payload_size, source_format);
    case kBdImageTransparentColumns:
      return DecodeTransparentColumns(target, source, payload_size, source_format);
    case kBdImagePaletteRows:
      return DecodePaletteRows(target, source, payload_size, source_format);
    case kBdImagePaletteColumns:
      return DecodePaletteColumns(target, source, payload_size, source_format);
    case 0x8000:
    case 0xc000:
      return 0;
    }
  }
  return (u8)flags;
}

// Expand a plain QFS bitmap directly when pitch equals byte width, otherwise copy from a temporary packed image.
// Preserve lock/row-query order and reject packets that would read or write beyond initialized image storage.
u8 BdImageArchive::DecodePlainBitmap(GxBuffer *target, const u8 *source, u32 payload_size) {
  if (!target->Lock(GxLockFlags::Bits))
    return 0;
  u8 *destination = static_cast<u8 *>(target->GetColorSurfaceBits());
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), written;
  u32 decoded_size;
  u64 image_size = static_cast<u64>(width) * height;
  if (!BdValidateQfsInput(source, payload_size, &decoded_size) || decoded_size > image_size ||
      (pitch != width && decoded_size != image_size)) {
    target->Unlock(GxLockFlags::Bits);
    return 0;
  }
  if (pitch != width) {
    u32 size = width * height;
    u8 stack[0x10000], *heap = size >= sizeof(stack) ? new u8[size] : nullptr;
    u8 *pixels = heap ? heap : stack;
    if (codec.Decompress(source, payload_size, pixels, &written)) {
      for (u32 row = height; row; --row) {
        memcpy(destination, pixels, width);
        destination += pitch;
        pixels += width;
      }
      target->Unlock(GxLockFlags::Bits);
      delete[] heap;
      return 1;
    }
    delete[] heap;
    target->Unlock(GxLockFlags::Bits);
    return 0;
  }
  bool result = codec.Decompress(source, payload_size, destination, &written);
  target->Unlock(GxLockFlags::Bits);
  return result;
}

// Decode literal WORDs and sentinel-one transparent runs across rows. Transparent stores reload the archive's
// live color key, while only literal colors pass through the buffer's conversion method.
u8 BdImageArchive::DecodeTransparentRows(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), span = width * 2;
  for (u32 row = height; row; --row) {
    usize end = destination + span;
    do {
      u16 pixel;
      memcpy(&pixel, source, sizeof(pixel));
      if (pixel == 1) {
        for (u32 left = source[2]; left; --left) {
          u16 key = color_key;
          memcpy(pixels + destination, &key, sizeof(key));
          destination += 2;
        }
        source += 3;
      } else {
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
        source += 2;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode the same sentinel-one packets down pitch-separated columns. Preserve the first packet for a zero
// height span, packet overshoot and each transparent store's live color-key read.
u8 BdImageArchive::DecodeTransparentColumns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(),
      span = pitch * height;
  for (u32 column = width; column; --column) {
    usize end = destination + span;
    do {
      u16 pixel;
      memcpy(&pixel, source, sizeof(pixel));
      if (pixel == 1) {
        for (u32 left = source[2]; left; --left) {
          u16 key = color_key;
          memcpy(pixels + destination, &key, sizeof(key));
          destination += pitch;
        }
        source += 3;
      } else {
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
        source += 2;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Resolve literal byte indices through the payload's WORD palette and decode escape runs across rows.
// Each escape packet captures the live color key before reading its count, even when that count is zero.
u8 BdImageArchive::DecodePaletteRows(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), span = width * 2;
  u8 escape = source[1];
  const u8 *palette = source + 2;
  usize input = 2 + 2 * source[0];
  for (u32 row = height; row; --row) {
    usize end = destination + span;
    do {
      u16 pixel;
      if (source[input] == escape) {
        pixel = color_key;
        for (u32 left = source[input + 1]; left; --left) {
          memcpy(pixels + destination, &pixel, sizeof(pixel));
          destination += 2;
        }
        input += 2;
      } else {
        memcpy(&pixel, palette + 2 * source[input], sizeof(pixel));
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
        ++input;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode palette indices and captured-color escape runs down columns. Palette header reads still occur when
// no columns are drawn, and only literal palette entries invoke pixel conversion.
u8 BdImageArchive::DecodePaletteColumns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height();
  u8 escape = source[1];
  const u8 *palette = source + 2;
  usize input = 2 + 2 * source[0];
  u32 span = pitch * height;
  for (u32 column = width; column; --column) {
    usize end = destination + span;
    do {
      u16 pixel;
      if (source[input] == escape) {
        pixel = color_key;
        for (u32 left = source[input + 1]; left; --left) {
          memcpy(pixels + destination, &pixel, sizeof(pixel));
          destination += pitch;
        }
        input += 2;
      } else {
        memcpy(&pixel, palette + 2 * source[input], sizeof(pixel));
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
        ++input;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

struct BdLegacyTexture {
  u32 id;
  const char *name;
};

// Stock version-6 texture identifiers and their original catalog names, in first-match order.
static const BdLegacyTexture kBdLegacyTextures[] = {
#include "archive_legacy_textures.inc"
};

// Load an asset record encountered while enumerating the archive and register it in the actual global catalog.
static void BdLoadGridAsset(RsKey *key, BdArchiveKeyContext *context) {
  BdIxf *archive = reinterpret_cast<BdIxf *>(context);
  BdAsset *asset = nullptr;
  archive->ReadAsset(key, &asset, 1);
  BdCatalog *catalog = BdGetCatalog();
  if (catalog && asset)
    catalog->AddAsset(asset);
}

// Load and register a definition before resolving its name in the actual global catalog.
static void BdLoadGridDefinition(RsKey *key, BdArchiveKeyContext *context) {
  BdIxf *archive = reinterpret_cast<BdIxf *>(context);
  BdDefinition *definition = nullptr;
  if (archive->ReadDefinition(key, &definition)) {
    BdCatalog *catalog = BdGetCatalog();
    if (catalog) {
      catalog->AddDefinition(definition);
      catalog->ResolveDefinition(&definition->name);
    }
    definition->Release();
  }
}

static void BdLowerGridName(ScString *name) {
  name->MakeLower();
}

// Resolve a version-6 numeric texture identifier through the original first-match table or custom-name pattern.
static void BdLegacyGridName(u32 id, ScString *name) {
  if (id == kBdLegacyLastTextureId) {
    name->FromChar("last");
    return;
  }
  for (u32 i = 0; i < sizeof(kBdLegacyTextures) / sizeof(kBdLegacyTextures[0]); ++i) {
    if (kBdLegacyTextures[i].id == id) {
      name->FromChar(kBdLegacyTextures[i].name);
      BdLowerGridName(name);
      return;
    }
  }
  char text[40];
  snprintf(text, sizeof(text), "CustomV6TextureID%d", static_cast<i32>(id));
  name->FromChar(text);
  BdLowerGridName(name);
}

static void BdStripGridNameSuffix(ScString *name) {
  const char *separator = name->Strlen() >= 10 ? strstr(name->Data(), " |") : nullptr;
  unsigned parsed;
  if (separator && separator != name->Data() && sscanf(separator + 2, "%8x", &parsed) == 1)
    name->Erase(static_cast<u32>(separator - name->Data()), static_cast<u32>(name->end - separator));
}

// A decoding path also verifies the packet stream before passing it to the original unbounded codec.
static bool BdReadGridCompressed(BdIxf *archive, RsStream *stream, u8 **out, u32 *decoded, bool decode) {
  u32 length = 0;
  stream->ReadU32(&length);
  u8 *compressed = new u8[length];
  bool valid = stream->ReadBlock(compressed, length);
  if (valid && !decode) {
    valid = length >= 6 && length >= (compressed[4] == 0x10 && compressed[5] == 0xfb ? 9u : 12u);
    if (valid)
      *decoded = archive->archive_codec.DecompressedSize(compressed);
    delete[] compressed;
    *out = nullptr;
    return valid;
  }
  valid = valid && BdValidateQfsInput(compressed, length, decoded);
  if (!valid) {
    delete[] compressed;
    return false;
  }
  u8 *cells = new u8[*decoded];
  u32 written = 0;
  archive->archive_codec.Decompress(compressed, length, cells, &written);
  valid = written == *decoded;
  delete[] compressed;
  if (!valid) {
    delete[] cells;
    return false;
  }
  *out = cells;
  return true;
}

// Read the versioned grid columns, cell payload and material table into the constructed game grid.
// Preserve discarded reads and early exits while checking inputs used by the unbounded cell decoder.
static bool BdReadGridBody(BdIxf *archive, RsStream *stream, BdGrid *grid, BdMaterials *materials, u8 version) {
  u8 rotation = 0, side = 0, view = 0, padding = 0;
  u16 height = 0, marker16 = 0;
  u32 count = 0, value = 0;
  if (version < 4 || version > 8)
    return true;
  bool rotation_read = stream->ReadU8(&rotation), side_read = stream->ReadU8(&side);
  bool view_read = stream->ReadU8(&view);
  if (version >= 6)
    stream->ReadU8(&padding);
  bool height_read = stream->ReadU16(&height);
  if (!side_read)
    return false;
  if (version <= 6 && side > 32)
    return true;
  if (!height_read)
    return false;
  if (version <= 6 && height > 512)
    return true;
  if (!rotation_read || (version >= 6 && !view_read))
    return false;
  if (version >= 7 && side > 48)
    side = 48;
  u32 actual_height = static_cast<u32>(height) + (version == 7 ? 1u : 0u);
  grid->SetupStorage(side, actual_height);
  grid->rotation = rotation;
  grid->side = side;
  grid->view_mode = version <= 5 ? side >> 3 : view;
  grid->height = static_cast<i32>(actual_height);
  stream->ReadU16(&marker16);
  if (!stream->ReadU32(&count) || (version <= 5 && count > 0x20000))
    return false;
  grid->cell_count = count;
  value = count;
  for (u32 i = 0; i < 5; ++i)
    stream->ReadU32(&value);
  if (version <= 5) {
    u16 *columns = new u16[static_cast<u32>(side) * side];
    bool columns_read = stream->ReadBlock(columns, 2u * side * side);
    if (!columns_read && side) {
      delete[] columns;
      return false;
    }
    for (u32 x = 0; x < side; ++x) {
      for (u32 y = 0; y < side; ++y) {
        u16 column = columns[x * side + y];
        grid->columns[x][y] = column;
        grid->render_columns[x][y] = column;
      }
    }
    delete[] columns;
    u8 *raw = nullptr;
    u32 size = 76u * count;
    if (version == 4) {
      raw = new u8[size];
      bool raw_read = stream->ReadBlock(raw, size);
      if (!raw_read && size) {
        delete[] raw;
        return false;
      }
    } else {
      u32 decoded = 0;
      if (!BdReadGridCompressed(archive, stream, &raw, &decoded, true))
        return false;
      if (decoded < size) {
        delete[] raw;
        return false;
      }
    }
    for (u32 i = 0; i < count; ++i)
      grid->cells[i].LoadLegacy(reinterpret_cast<const BdLegacyCell *>(raw + i * 76));
    delete[] raw;
  } else {
    for (u32 x = 0; x < side; ++x)
      if (!stream->ReadBlock(grid->columns[x], 4u * side))
        return false;
    u8 *raw = nullptr;
    u32 decoded = 0;
    stream->ReadU32(&value);
    if (!BdReadGridCompressed(archive, stream, &raw, &decoded, grid->cells != nullptr))
      return false;
    if (grid->cells && decoded > kBdGridCompressedCapacityLimit) {
      delete[] raw;
      return false;
    }
    if (grid->cells)
      memcpy(grid->cells, raw, decoded);
    delete[] raw;
    stream->ReadU32(&value);
    stream->ReadU32(&value);
    u32 material_count = version == 6 ? 256 : value;
    // Stock reuses this slot across entries; initialize it to avoid its first failed-read undefined value.
    u32 texture_id = 0;
    for (u32 i = 0; i < material_count; ++i) {
      ScString name(version == 6 ? "last" : "null");
      u32 id = value, uses = 0;
      if (version == 6) {
        stream->ReadU32(&texture_id);
        stream->ReadU32(&uses);
        if (!i)
          continue;
        id = texture_id;
        BdLegacyGridName(id, &name);
      } else {
        bool converted;
        RsReadLegacyText(stream, &name, BdArchiveTextPage(), &converted);
        stream->ReadU32(&value);
        if (!converted)
          return false;
        id = value;
        if (!i)
          continue;
        BdStripGridNameSuffix(&name);
        BdLowerGridName(&name);
      }
      bool empty = name.Strlen() == 4 && !memcmp(name.Data(), version == 6 ? "last" : "null", 4);
      BdAsset *asset = nullptr;
      if (!empty && archive->catalog->FindAsset(&name, &asset)) {
        if (i >= 256)
          return false;
        materials->Set(i, asset);
        if (version == 6 || version == 7)
          materials->SetUse(i, version == 6 ? uses : id);
      }
    }
    stream->ReadU32(&value);
  }
  grid->free_head = count;
  if (version == 7)
    grid->NormalizeImportedGround();
  return true;
}

// Read a saved building grid after restoring its associated assets and definitions to the global catalog.
// Compressed-payload failures are rejected before incomplete grid data is consumed.
u8 BdIxf::ReadGrid(const RsKey *key, BdGrid **out) {
  BdArchiveKeyContext *context = reinterpret_cast<BdArchiveKeyContext *>(this);
  EnumAssets(BdLoadGridAsset, context);
  EnumDefinitions(BdLoadGridDefinition, context);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!DoOpenRecord(*key, &record))
    return 0;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    DoCloseRecord(&record);
    return 0;
  }
  BdGrid *grid = new BdGrid;
  grid->AddRef();
  BdMaterials *materials = nullptr;
  grid->GetMaterials(&materials);
  u8 version = 0;
  bool read = stream->ReadU8(&version) && BdReadGridBody(this, stream, grid, materials, version);
  if (read) {
    grid->RecountMaterials();
    for (u32 i = 1; i < 256; ++i) {
      BdAsset *asset = nullptr;
      u32 uses = 0;
      materials->GetUse(i, &uses);
      if (materials->Get(i, &asset))
        catalog->IndexAsset(&asset->name);
    }
    for (u32 i = 1; i < 256; ++i) {
      BdAsset *asset = nullptr;
      if (materials->Get(i, &asset) && asset->IsKind8Through16()) {
        BdDefinition *definition = nullptr;
        if (catalog->FindDefinition(&asset->name, &definition)) {
          for (u32 j = 0; j < 3; ++j)
            if (!definition->variant_ids[j])
              catalog->IndexAsset(&definition->variants[j]);
          definition->Release();
        }
      }
    }
    for (u32 i = 1; i < 256; ++i) {
      BdAsset *asset = nullptr;
      if (materials->Get(i, &asset) && asset->IsKind8Through16())
        catalog->ResolveDefinition(&asset->name);
    }
  }
  materials->Release();
  stream->Release();
  DoCloseRecord(&record);
  if (!read) {
    grid->Release();
    return 0;
  }
  grid->ResetChanges();
  *out = grid;
  return 1;
}

static u32 BdImageArchiveWord(const u8 *bytes) {
  return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
      static_cast<u32>(bytes[3]) << 24;
}

// Read a local image-archive record and create the original buffer or compressed image-builder result.
u8 BdImageArchive::ReadImage(GxBuffer **out, const RsKey *key) {
  RsRecord *record = nullptr;
  RsRecordData *data;
  GxBuffer *target = nullptr;
  u8 *allocation = nullptr;
  if (!DoOpenRecord(*key, &record) || !record->QueryInterface(IID_RsRecordData, reinterpret_cast<void **>(&data)))
    return 0;
  u32 size = 0;
  data->GetSize(&size);
  u8 local[0xc010] = {}, *input = local;
  if (size > sizeof(local)) {
    allocation = new u8[size]();
    input = allocation;
  }
  u8 read = data->ReadBlock(input, size);
  data->Release();
  if (!read || size < 16) {
    DoCloseRecord(&record);
    delete[] allocation;
    return 0;
  }
  u8 pixel_format = input[0], kind = input[1];
  u32 flags = BdImageArchiveWord(input + 4), width = BdImageArchiveWord(input + 8);
  u32 height = BdImageArchiveWord(input + 12);
  const u8 *pixels = input + 16;
  size -= 16;
  if (!kind) {
    BdGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&target));
    bool indexed = (flags & kBdImageIndexedFormatFlag) != 0;
    if (target->Init(width, height, GxColorType(static_cast<GxColorType::Color>(indexed ? 3 : 4)), nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      DecodePixels(target, pixels, size, flags, pixel_format);
      DoCloseRecord(&record);
      *out = target;
      if (!indexed) {
        target->SetTransparency(color_key);
        GxImageBuilder *builder = nullptr;
        if (BdGetCom()->GetClassObject(
                kGxImageBuilderClassId, IID_GxImageBuilder, reinterpret_cast<void **>(&builder))) {
          builder->ConvertStandardBufferToData(target);
          target->Release();
          *out = builder->AsIGZBuffer();
        }
      }
      delete[] allocation;
      return 1;
    }
  } else if (kind == 1 && !(flags & 0x8000) && (flags & 0x80000)) {
    u32 decoded_size;
    if (!BdValidateQfsInput(pixels, size, &decoded_size)) {
      DoCloseRecord(&record);
      *out = nullptr;
      delete[] allocation;
      return 0;
    }
    GxImageBuilder *builder = nullptr;
    BdGetCom()->GetClassObject(kGxImageBuilderClassId, IID_GxImageBuilder, reinterpret_cast<void **>(&builder));
    target = builder->AsIGZBuffer();
    u8 *decoded = new u8[decoded_size];
    if (codec.Decompress(pixels, size, decoded, &decoded_size)) {
      builder->ImportFromData(decoded, 1);
      *out = target;
      i32 desired = target_format;
      if (static_cast<i32>(target->GetColorType().color) != desired) {
        u32 actual = static_cast<u32>(target->GetColorType().color);
        target->Convert16BitBufferFromFormatToFormat(
            static_cast<GxColorType::Color>(actual), static_cast<GxColorType::Color>(static_cast<u32>(desired)));
      }
      DoCloseRecord(&record);
      delete[] allocation;
      return 1;
    }
  }
  if (target)
    target->Release();
  DoCloseRecord(&record);
  *out = nullptr;
  delete[] allocation;
  return 0;
}

// Encode an image-builder block and write its paired sprite bounds and compressed image records.
// The installed nonnull-bounds path derives its isometric rectangle from the image dimensions.
u8 BdArchive::WriteSprite(const RsKey *key, GxBuffer *image, const BdRect *) {
  GxImage builder;
  u32 width = image->Width(), height = image->Height();
  i32 format = static_cast<i32>(image->GetColorType().color);
  i16 rectangle[4] = {
      0, static_cast<i16>(height - (width >> 2)), static_cast<i16>(width), static_cast<i16>(width >> 2)};
  u32 image_key_color;
  if (image->GetTransparentColor(&image_key_color))
    builder.SetTransparency(image_key_color);
  u8 *encoded = nullptr;
  u32 encoded_size = 0;
  if (!builder.ConvertStandardBufferToData(image, &encoded, &encoded_size)) {
    delete[] encoded;
    return 0;
  }
  u32 capacity = 10u * encoded_size / 9u + 1000u;
  u32 input_length = static_cast<i32>(encoded_size) > 0 ? encoded_size : 0;
  usize allocation_size = static_cast<usize>(input_length) + input_length / 4u + 64u;
  if (allocation_size < capacity)
    allocation_size = capacity;
  u8 *compressed = new u8[allocation_size];
  i32 option = 1;
  u32 written = BdQfsEncode(encoded, static_cast<i32>(encoded_size), compressed + 4, &option) + 4;
  memcpy(compressed, &written, sizeof(written));
  delete[] encoded;
  if (written > capacity) {
    delete[] compressed;
    return 0;
  }
  RsKey rectangle_key(1, key->group, key->instance), image_key(0, key->group, key->instance);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (DoCreateNewRecord(rectangle_key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteBlock(rectangle, sizeof(rectangle));
      stream->Release();
    }
    DoCloseRecord(&record);
  }
  u8 result = 0;
  record = nullptr;
  stream = nullptr;
  if (DoCreateNewRecord(image_key, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU8(static_cast<u8>(format));
      stream->WriteU8(1);
      stream->WriteU16(0);
      stream->WriteU32(0x80000);
      stream->WriteU32(width);
      stream->WriteU32(height);
      stream->WriteBlock(compressed, written);
      stream->Release();
      result = 1;
    }
    DoCloseRecord(&record);
  }
  delete[] compressed;
  return result;
}

// Write the twenty-entry sprite directory, then submit all sixteen image and bounds pairs.
// The original returns the directory close byte and does not fold individual sprite results into it.
u8 BdArchive::WriteFrames(u32 group, u32 instance, const BdExportFrames *frames) {
  RsKey directory(0x6300, group, instance);
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  u8 result = 1;
  if (DoCreateNewRecord(directory, &record)) {
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->WriteU8('B');
      stream->WriteU8('I');
      stream->WriteU8('N');
      stream->WriteU8(13);
      stream->WriteU32(1);
      stream->WriteU32(29440);
      stream->WriteU8(5);
      stream->WriteU8(4);
      stream->WriteU8(4);
      stream->WriteU8(20);
      stream->WriteU8(1);
      stream->WriteU8(0);
      stream->WriteU32(0);
      stream->WriteU16(20);
      u32 base = instance * 32;
      for (u32 i = 0; i < 20; ++i) {
        stream->WriteI16(static_cast<i16>(i));
        stream->WriteU32(group);
        stream->WriteU32(base | (i < 16 ? i : i - 4));
        stream->WriteI8(i < 16 ? 0 : 2);
        stream->WriteI16(0);
        stream->WriteI16(0);
      }
    }
    result = DoCloseRecord(&record);
  }
  for (u32 i = 0; i < 16; ++i) {
    RsKey image(0, group, (instance * 32) | i);
    WriteSprite(&image, frames->images[i], &frames->bounds[i]);
  }
  return result;
}
