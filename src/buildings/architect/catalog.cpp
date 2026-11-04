#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <iterator>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/document.h"
#include "buildings/architect/files.h"
#include "common/api.h"
#include "common/locale.h"
#include "common/path.h"
#include "common/utf8.h"
#include "resources/api.h"
#include "resources/file.h"

static void BdLowerCatalogName(ScString *text) {
  text->MakeLower();
}

// Register a named asset unless both its name and ID already resolve in the catalog.
// Selected image-backed kinds immediately load and index the named entry even if an empty name prevented appending it.
u8 BdCatalog::AddAsset(BdAsset *asset) {
  if (!asset)
    return 0;
  BdAsset *named = nullptr, *indexed = nullptr;
  bool found_name = FindAsset(&asset->name, &named), found_id = GetAsset(asset->definition_id, &indexed);
  u8 appended = 0;
  if (!found_name || !found_id) {
    i8 kind = asset->kind;
    if (asset->name.Strlen()) {
      *assets.Append() = asset;
      appended = 1;
    }
    if (kind == 3 || kind == 6 || kind == 7 || kind == 16)
      IndexAsset(&asset->name);
  }
  return appended;
}

// Construct an asset with its original separate name, kind, path, flag, image and optional rectangle inputs.
// Catalog insertion decides whether it is retained; the original pure retain/release shim does not delete rejected storage.
u8 BdCatalog::CreateAsset(const ScString *name, i8 kind, const ScString *path, u32 flags, GxBuffer *image,
    const BdRect *icon, const BdRect *preview) {
  BdAsset *asset = new BdAsset;
  asset->name = *name;
  asset->path = *path;
  asset->flags = flags;
  asset->kind = kind;
  if (image)
    asset->SetImage(image);
  if (icon)
    asset->icon_bounds = *icon;
  if (preview)
    asset->preview_bounds = *preview;
  return AddAsset(asset);
}

// Run the real lazy image loader before finding the named asset and inserting its current unsigned image key.
// Duplicate keys retain their original borrowed pointer, matching first-wins tree insertion without extra asset ownership.
u8 BdCatalog::IndexAsset(const ScString *name) {
  BdLoadAsset(name, this, nullptr);
  BdAsset *asset = nullptr;
  if (!FindAsset(name, &asset))
    return 0;
  u32 first = 0, last = asset_keys.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    if (asset_keys.data[middle].id < asset->definition_id)
      first = middle + 1;
    else
      last = middle;
  }
  if (first != asset_keys.size && asset_keys.data[first].id == asset->definition_id)
    return 0;
  asset_keys.Append();
  for (u32 i = asset_keys.size - 1; i > first; --i)
    asset_keys.data[i] = asset_keys.data[i - 1];
  asset_keys.data[first] = {asset->definition_id, asset};
  return 1;
}

// Visit asset names with the requested byte kind in original insertion order and return the number visited.
// The original captures the vector end before invoking any callbacks.
u32 BdCatalog::EnumerateAssets(u32 kind, BdAssetVisitor visitor, BdAssetVisitContext *context) {
  if (!assets.size)
    return 0;
  BdAsset **current = assets.data, **end = assets.data + assets.size;
  u32 count = 0;
  while (current != end) {
    BdAsset *asset = *current++;
    if (static_cast<u8>(asset->kind) == kind) {
      visitor(&asset->name, this, context);
      ++count;
    }
  }
  return count;
}

static void BdIndexAssetVisitor(const ScString *name, BdCatalog *catalog, BdAssetVisitContext *) {
  catalog->IndexAsset(name);
}

static void BdResolveDefinitionVisitor(const ScString *name, BdCatalog *catalog, BdAssetVisitContext *) {
  catalog->ResolveDefinition(name);
}

// Index every asset of the requested kind, then resolve definitions for kinds 8 through 16.
// Kind 3 is already available without either pass, and kinds above 16 are rejected.
u8 BdCatalog::IndexKind(u32 kind) {
  if (kind == 3)
    return 1;
  if (kind > 16)
    return 0;
  BdAssetVisitContext *context = reinterpret_cast<BdAssetVisitContext *>(this);
  EnumerateAssets(kind, BdIndexAssetVisitor, context);
  if (kind > 7)
    EnumerateAssets(kind, BdResolveDefinitionVisitor, context);
  return 1;
}

// Clear asset keys before releasing definition-index ownership, then clear and delete assets in insertion order.
// Capture the deletion count before releases; definition lists, groups and source records keep their separate lifetimes.
void BdCatalog::ClearAssets() {
  asset_keys.Clear();
  for (u32 i = definition_keys.size; i; --i)
    if (definition_keys.data[i - 1].definition)
      definition_keys.data[i - 1].definition->Release();
  definition_keys.Clear();
  u32 count = assets.size;
  for (u32 i = 0; i < count; ++i) {
    BdAsset *asset = assets.data[i];
    asset->SetImage(nullptr);
    delete asset;
  }
  assets.Clear();
}

// Clear stored images for every indexed asset except the original kind-3 materials.
// The source traverses the ordered asset index and discards its final iterator value.
void BdCatalog::ClearRenderedImages() {
  for (u32 i = 0; i < asset_keys.size; ++i) {
    BdAsset *asset = asset_keys.data[i].asset;
    if (asset && asset->kind != 3)
      asset->SetImage(nullptr);
  }
}

// The catalog sorts reverse iterators with this ascending relation, producing descending physical label order.
static bool BdCompositionLess(const BdComposition *first, const BdComposition *second) {
  u32 first_length = first->name.Strlen(), second_length = second->name.Strlen();
  int result = memcmp(first->name.begin, second->name.begin, first_length < second_length ? first_length : second_length);
  return result < 0 || (!result && first_length < second_length);
}

// Read the catalog's seven-field definitions and measure placements using available opaque image bounds.
// A missing asset or zero width substitutes16 pixels in both dimensions before copying into the selected group.
u8 BdCatalog::BuildDefinitionComposition(u32 group, const ScString *path) {
  BdComposition composition;
  ReadDefinitionFile(&composition, path);
  i32 height = 0, width = 0;
  for (u32 i = 0; i < composition.parts.size; ++i) {
    BdPart *part = composition.parts.data[i];
    BdAsset *asset = nullptr;
    i32 part_width = 16, part_height = 16;
    if (FindAsset(&part->name, &asset)) {
      i32 image_width = asset->opaque_bounds.right - asset->opaque_bounds.left;
      if (image_width) {
        part_width = image_width;
        part_height = asset->opaque_bounds.bottom - asset->opaque_bounds.top;
      }
    }
    i32 right = 16 * part->x + part_width, bottom = 16 * part->y + part_height;
    if (right > width)
      width = right;
    if (bottom > height)
      height = bottom;
  }
  composition.height = height;
  composition.width = width;
  BdGroup *selected = groups.data[group];
  *selected->compositions.Append() = new BdComposition(composition);
  std::sort(std::make_reverse_iterator(selected->compositions.data + selected->compositions.size),
      std::make_reverse_iterator(selected->compositions.data), BdCompositionLess);
  return 1;
}

// Read a placement palette and measure preview rectangles at16-pixel coordinate intervals.
// Width and height independently substitute16 pixels when missing or zero before storing and sorting the group copy.
u8 BdCatalog::BuildComposition(u32 group, const ScString *path) {
  BdComposition composition;
  BdReadCompositionFile(&composition, path);
  i32 height = 0, width = 0;
  for (u32 i = 0; i < composition.parts.size; ++i) {
    BdPart *part = composition.parts.data[i];
    BdAsset *asset = nullptr;
    i32 part_width = 0, part_height = 0;
    if (FindAsset(&part->name, &asset)) {
      part_width = asset->preview_bounds.right - asset->preview_bounds.left;
      part_height = asset->preview_bounds.bottom - asset->preview_bounds.top;
    }
    if (!part_width)
      part_width = 16;
    if (!part_height)
      part_height = 16;
    i32 right = 16 * part->x + part_width, bottom = 16 * part->y + part_height;
    if (right > width)
      width = right;
    if (bottom > height)
      height = bottom;
  }
  composition.height = height;
  composition.width = width;
  BdGroup *selected = groups.data[group];
  *selected->compositions.Append() = new BdComposition(composition);
  std::sort(std::make_reverse_iterator(selected->compositions.data + selected->compositions.size),
      std::make_reverse_iterator(selected->compositions.data), BdCompositionLess);
  return 1;
}

static void BdQuotedName(ScString *name, const char *token) {
  u32 length = static_cast<u32>(strlen(token));
  u32 count = length > 1 ? length - 2 : 0;
  count = ScUtf8Prefix(token + 1, length ? length - 1 : 0, count);
  name->FromChar(token + 1, count);
  BdLowerCatalogName(name);
}

// Retain a new definition in insertion order unless an exact name is already present.
u8 BdCatalog::AddDefinition(BdDefinition *definition) {
  if (definition) {
    BdDefinition *existing = nullptr;
    if (FindDefinition(&definition->name, &existing)) {
      existing->Release();
    } else {
      ++definition->references;
      ++definition->references;
      *definitions.Append() = definition;
      definition->Release();
    }
  }
  return 0;
}

// Terminate the bytes actually read so the tokenizer never consumes unavailable file data.
// Installed catalog text is ASCII, and native additions use UTF-8. Decline malformed text without guessing a page.
static char *BdReadCatalogText(RsFile *file, u32 length) {
  if (length == UINT32_MAX)
    return nullptr;
  char *text = new char[static_cast<usize>(length) + 1];
  u32 read = length;
  if (!file->Read(text, &read)) {
    delete[] text;
    return nullptr;
  }
  text[read] = 0;
  if (!ScUtf8Validate(text, static_cast<u32>(strlen(text)))) {
    delete[] text;
    return nullptr;
  }
  return text;
}

// Extract the final path component without its last extension using the original splitpath's255-byte name limit.
static void BdCatalogBasename(ScString *name, const ScString *path) {
  const char *first = path->begin, *end = first + strlen(first);
  if (*first && first[1] == ':')
    first += 2;
  for (const char *p = first; p != end; ++p)
    if (*p == '/' || *p == '\\')
      first = p + 1;
  const char *last = end;
  for (const char *p = end; p != first;) {
    --p;
    if (*p == '.') {
      last = p;
      break;
    }
  }
  u32 count = static_cast<u32>(last - first);
  count = ScUtf8Prefix(first, count, count > 255 ? 255 : count);
  name->FromChar(first, count);
}

void BdCatalog::ReadDefinitionFile(BdComposition *composition, const ScString *path) {
  if (!RsFile::FileExists(*path))
    return;
  RsFile file(*path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return;
  u32 length = file.Size();
  if (length) {
    char *text = BdReadCatalogText(&file, length);
    if (!text)
      return;
    BdPart part;
    BdDefinition *definition = nullptr;
    u8 position = 0;
    for (char *token = strtok(text, ",:\r\n"); token; token = strtok(nullptr, ",:\r\n")) {
      if (!definition) {
        definition = new BdDefinition;
        ++definition->references;
      }
      u8 field = position++;
      if (field < 3) {
        i32 value = static_cast<i32>(atol(token));
        if (static_cast<u32>(value) >= (field == 2 ? 20u : 40u))
          value = 0;
        if (field == 0) {
          part.x = 0;
          part.y = 0;
          definition->size.x = value;
        } else if (field == 1) {
          definition->size.y = value;
        } else {
          definition->size.z = value;
        }
      } else if (field == 3) {
        BdQuotedName(&part.name, token);
        *composition->parts.Append() = new BdPart(part);
        definition->name.FromChar(part.name.begin, part.name.Strlen());
      } else if (field < 6) {
        ScString name;
        BdQuotedName(&name, token);
        definition->variants[field - 4].FromChar(name.begin, name.Strlen());
      } else {
        if (field == 6) {
          ScString name;
          BdQuotedName(&name, token);
          definition->variants[2].FromChar(name.begin, name.Strlen());
        }
        AddDefinition(definition);
        definition->Release();
        definition = nullptr;
        position = 0;
      }
    }
    delete[] text;
    BdCatalogBasename(&composition->name, path);
  }
}

// Resolve a primary asset and its three named variants, copying each available image's bounds into its display rectangles.
// Publish the primary ID in the first-wins definition index and retain only newly inserted index ownership.
u8 BdCatalog::ResolveDefinition(const ScString *name) {
  BdAsset *primary = nullptr;
  if (!FindAsset(name, &primary))
    return 0;
  primary->preview_bounds = primary->opaque_bounds;
  primary->icon_bounds = primary->opaque_bounds;
  BdDefinition *definition = nullptr;
  if (!FindDefinition(name, &definition))
    return 0;
  for (u32 i = 0; i < 3; ++i) {
    BdAsset *variant = nullptr;
    if (FindAsset(&definition->variants[i], &variant)) {
      variant->preview_bounds = variant->opaque_bounds;
      variant->icon_bounds = variant->opaque_bounds;
      definition->variant_ids[i] = variant->definition_id;
    }
  }
  definition->id = primary->definition_id;
  ++definition->references;
  ++definition->references;
  u32 first = 0, last = definition_keys.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    if (definition_keys.data[middle].id < definition->id)
      first = middle + 1;
    else
      last = middle;
  }
  u8 inserted = first == definition_keys.size || definition_keys.data[first].id != definition->id;
  if (inserted) {
    definition_keys.Append();
    for (u32 i = definition_keys.size - 1; i > first; --i)
      definition_keys.data[i] = definition_keys.data[i - 1];
    definition_keys.data[first] = {definition->id, definition};
    ++definition->references;
  }
  definition->Release();
  definition->Release();
  definition->Release();
  return inserted;
}

void BD_STDCALL BdReadCompositionFile(BdComposition *composition, const ScString *path) {
  if (!RsFile::FileExists(*path))
    return;
  RsFile file(*path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return;
  u32 length = file.Size();
  if (length) {
    char *text = BdReadCatalogText(&file, length);
    if (!text)
      return;
    BdPart part;
    u8 position = 0;
    for (char *token = strtok(text, ",=\n"); token; token = strtok(nullptr, ",=\n")) {
      if (position == 0) {
        part.x = static_cast<u8>(atol(token));
        position = 1;
      } else if (position == 1) {
        part.y = static_cast<u8>(atol(token));
        position = 2;
      } else {
        u32 count = static_cast<u32>(strlen(token));
        count = count >= 3 ? count - 3 : count - 1;
        count = ScUtf8Prefix(token + 1, static_cast<u32>(strlen(token + 1)), count);
        ScString name(token + 1, count);
        BdLowerCatalogName(&name);
        part.name = name;
        *composition->parts.Append() = new BdPart(part);
        position = 0;
      }
    }
    delete[] text;
    BdCatalogBasename(&composition->name, path);
  }
}

// Start empty catalog indices and source lists, initialize the image-key table, and append255 independent empty groups.
// Copying each temporary preserves the stored empty label's original exact-size string allocation.
BdCatalog::BdCatalog() {
  BdInitializeCrc();
  for (u32 i = 0; i < 255; ++i) {
    BdGroup group;
    *groups.Append() = new BdGroup(group);
  }
}

// Release definition-list ownership before definition-index ownership, traversing index keys in descending order.
// Then destroy groups and source records in original forward order; asset image clearing belongs to the separate clear method.
BdCatalog::~BdCatalog() {
  for (u32 i = 0; i < definitions.size; ++i)
    if (definitions.data[i])
      definitions.data[i]->Release();
  for (u32 i = definition_keys.size; i; --i)
    if (definition_keys.data[i - 1].definition)
      definition_keys.data[i - 1].definition->Release();
  for (u32 i = 0; i < groups.size; ++i)
    delete groups.data[i];
  for (u32 i = 0; i < definition_files.size; ++i)
    delete definition_files.data[i];
  for (u32 i = 0; i < placement_files.size; ++i)
    delete placement_files.data[i];
}

// Resolve a nonzero unsigned asset ID, publishing the borrowed stored pointer on an exact key match.
// The original asset-retain shim is pure; null outputs fail before any index lookup.
bool BdCatalog::GetAsset(u32 id, BdAsset **out) {
  if (!out)
    return false;
  *out = nullptr;
  if (!id)
    return false;
  u32 first = 0, last = asset_keys.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    if (asset_keys.data[middle].id < id)
      first = middle + 1;
    else
      last = middle;
  }
  if (first == asset_keys.size || id < asset_keys.data[first].id)
    return false;
  *out = asset_keys.data[first].asset;
  return true;
}

// Resolve a nonzero unsigned definition ID and retain its actual reference field before publishing the pointer.
// Missing IDs clear a supplied output; a found record is required to contain its real definition receiver.
bool BdCatalog::GetDefinition(u32 id, BdDefinition **out) {
  if (!out)
    return false;
  *out = nullptr;
  if (!id)
    return false;
  u32 first = 0, last = definition_keys.size;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    if (definition_keys.data[middle].id < id)
      first = middle + 1;
    else
      last = middle;
  }
  if (first == definition_keys.size || id < definition_keys.data[first].id)
    return false;
  BdDefinition *definition = definition_keys.data[first].definition;
  ++definition->references;
  *out = definition;
  return true;
}

// Search asset insertion order backwards using exact byte-string equality, so the last matching name wins.
// The returned asset remains borrowed, matching the original pure retain shim.
bool BdCatalog::FindAsset(const ScString *name, BdAsset **out) {
  if (!out)
    return false;
  *out = nullptr;
  for (u32 i = assets.size; i; --i) {
    BdAsset *asset = assets.data[i - 1];
    usize length = static_cast<usize>(name->end - name->begin);
    if (length == static_cast<usize>(asset->name.end - asset->name.begin) &&
        !memcmp(name->begin, asset->name.begin, length)) {
      *out = asset;
      return true;
    }
  }
  return false;
}

// Search definitions backwards by exact name bytes, retaining the last matching receiver before publishing it.
// Empty lists fail without reading the input name, as in the original vector traversal.
bool BdCatalog::FindDefinition(const ScString *name, BdDefinition **out) {
  if (!out)
    return false;
  *out = nullptr;
  for (u32 i = definitions.size; i; --i) {
    BdDefinition *definition = definitions.data[i - 1];
    usize length = static_cast<usize>(name->end - name->begin);
    if (length == static_cast<usize>(definition->name.end - definition->name.begin) &&
        !memcmp(name->begin, definition->name.begin, length)) {
      ++definition->references;
      *out = definition;
      return true;
    }
  }
  return false;
}

// Native storage replaces only the SDK's general-purpose name list, preserving Win32 insertion order.
class BdFileNames {
public:
  ScArray<ScString *> names;
  ~BdFileNames();
  void Read(const ScString &path, bool directories);
};

BdFileNames::~BdFileNames() {
  for (u32 i = 0; i < names.size; ++i)
    delete names.data[i];
}

// Collect actual ANSI directory entries without sorting; directory scans include dot and exclude the parent.
// The original wrapper supplies dot at the front when a successful Win32 scan omitted that entry.
void BdFileNames::Read(const ScString &path, bool directories) {
  ScDirectory directory(path, false, false, false);
  ScDirectoryNames found;
  if (!directory.ReadNames(&found, directories ? 5 : 8))
    return;
  for (u32 i = 0; i < found.entries.size; ++i)
    *names.Append() = new ScString(*found.entries.data[i]);
}

// Close a successful search immediately and preserve the observed byte result.
u8 BdDirectoryExists(const ScString *path) {
  return ScDirectory::DoesDirectoryExist(*path);
}

// Append one real group/path record, retaining the original separate placement and definition source lists.
static void BdRememberSource(ScArray<BdFile *> *files, u32 group, const ScString &path) {
  BdFile *file = new BdFile;
  file->id = group;
  file->path = path;
  *files->Append() = file;
}

static bool BdMetadataSuffix(const ScString &name, const char *suffix) {
  u32 length = name.Strlen();
  return length >= 4 && !memcmp(name.begin + length - 4, suffix, 4);
}

// Enumerate the root/current directory and immediate children, recording metadata before registering their images.
u8 BdCatalog::EnumerateFamily(u32 group, const ScString *path) {
  if (!BdDirectoryExists(path))
    return 0;
  BdFileNames directories;
  directories.Read(*path, true);
  for (u32 i = 0; i < directories.names.size; ++i) {
    ScString *directory = directories.names.data[i];
    ScString folder(*path);
    folder.Append(directory->begin);
    BdFileNames files;
    files.Read(folder, false);
    ScString prefix(*path);
    if (strcmp(directory->begin, ".")) {
      prefix.Append(directory->begin);
      prefix.Append("\\");
    }
    for (u32 j = 0; j < files.names.size; ++j) {
      ScString lower(*files.names.data[j]);
      BdLowerCatalogName(&lower);
      if (BdMetadataSuffix(lower, ".met") || BdMetadataSuffix(lower, ".plt")) {
        ScString full(prefix);
        full.Append(files.names.data[j]->begin);
        BdRememberSource(BdMetadataSuffix(lower, ".met") ? &definition_files : &placement_files, group, full);
      }
    }
    for (u32 j = 0; j < files.names.size; ++j) {
      ScString full(prefix);
      full.Append(files.names.data[j]->begin);
      RegisterImage(static_cast<i8>(group), &full);
    }
  }
  return 1;
}

static bool BdHasCatalogSuffix(const ScString &path, const char *suffix) {
  u32 length = path.Strlen();
  return length >= 4 && !memcmp(path.begin + length - 4, suffix, 4);
}

// Register a BMP/Targa path after lowercasing three String copies, preserving its separate original asset kind.
u8 BdCatalog::RegisterImage(i8 kind, const ScString *path) {
  ScString lower(*path);
  BdLowerCatalogName(&lower);
  ScString test(lower);
  BdLowerCatalogName(&test);
  bool bitmap = BdHasCatalogSuffix(test, ".bmp"), targa = BdHasCatalogSuffix(test, ".tga");
  if (!bitmap && !targa)
    return 0;
  ScString stored(lower), name;
  BdLowerCatalogName(&stored);
  char drive[256], directory[256], file[256], extension[256];
  BdSplitPath(lower.begin, drive, directory, file, extension);
  name.FromChar(file);
  name.Append(extension);
  return CreateAsset(&name, kind, &stored, bitmap ? 2 : 4, nullptr, nullptr, nullptr);
}

// Traverse matching placement sources in stored order, preserving the last builder's observed byte or zero for no match.
u8 BdCatalog::BuildPlacementFiles(u32 group) {
  u8 result = 0;
  u32 count = placement_files.size;
  BdFile **files = placement_files.data;
  for (u32 i = 0; i < count; ++i) {
    BdFile *file = files[i];
    if (file->id == group)
      result = BuildComposition(group, &file->path);
  }
  return result;
}

// Traverse matching definition sources in stored order without sorting or deduplicating the source records.
u8 BdCatalog::BuildDefinitionFiles(u32 group) {
  u8 result = 0;
  u32 count = definition_files.size;
  BdFile **files = definition_files.data;
  for (u32 i = 0; i < count; ++i) {
    BdFile *file = files[i];
    if (file->id == group)
      result = BuildDefinitionComposition(group, &file->path);
  }
  return result;
}
