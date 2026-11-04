#ifndef SC3_BUILDINGS_ARCHITECT_BD_CATALOG_H
#define SC3_BUILDINGS_ARCHITECT_BD_CATALOG_H

#include "buildings/architect/document.h"
#include "common/sc3_array.h"

class BdAsset;
class BdDefinition;
class GxBuffer;
class ScString;
class BdCatalog;
struct BdAssetVisitContext;
using BdAssetVisitor = void (*)(const ScString *name, BdCatalog *catalog, BdAssetVisitContext *context);

struct BdAssetKey {
  u32 id;
  BdAsset *asset;
};

struct BdDefinitionKey {
  u32 id;
  BdDefinition *definition;
};

// The catalog owns source records, composition groups, retained definition indices, and insertion-order resource lists.
// Sorted key arrays replace only the original STL tree machinery.
class BdCatalog {
public:
  BdCatalog();
  ~BdCatalog();
  bool GetAsset(u32 id, BdAsset **out);
  bool GetDefinition(u32 id, BdDefinition **out);
  bool FindAsset(const ScString *name, BdAsset **out);
  bool FindDefinition(const ScString *name, BdDefinition **out);
  // The original private source return declaration is unresolved; all observed calls discard its zero byte result.
  u8 AddDefinition(BdDefinition *definition);
  // The private return declaration remains unresolved; the observed byte is the definition-index insertion result.
  u8 ResolveDefinition(const ScString *name);
  // Private builders return an observed one byte; source Boolean spelling and group-index signedness remain unresolved.
  u8 BuildDefinitionComposition(u32 group, const ScString *path);
  u8 BuildComposition(u32 group, const ScString *path);
  // Registration candidates preserve the original byte results; private bool versus byte spelling remains unresolved.
  u8 AddAsset(BdAsset *asset);
  u8 CreateAsset(const ScString *name, i8 kind, const ScString *path, u32 flags, GxBuffer *image, const BdRect *icon,
      const BdRect *preview);
  u8 IndexAsset(const ScString *name);
  // The callback count is a DWORD; its private source signedness remains unresolved.
  u32 EnumerateAssets(u32 kind, BdAssetVisitor visitor, BdAssetVisitContext *context);
  // The private Boolean/byte declaration remains unresolved; the installed result is zero or one.
  u8 IndexKind(u32 kind);
  // Native effect spelling; the private clear method's final vector register is not a normal game result.
  void ClearAssets();
  // Installed callers discard the original iterator result after these effects.
  void ClearRenderedImages();
  void ReadDefinitionFile(BdComposition *composition, const ScString *path);

  // Private source candidates retain observed bytes; original bool/byte spelling and group signedness remain unresolved.
  u8 RegisterImage(i8 kind, const ScString *path);
  u8 EnumerateFamily(u32 group, const ScString *path);
  u8 BuildPlacementFiles(u32 group);
  u8 BuildDefinitionFiles(u32 group);

  ScArray<BdFile *> placement_files, definition_files;
  ScArray<BdGroup *> groups;
  ScArray<BdAssetKey> asset_keys;
  ScArray<BdDefinitionKey> definition_keys;
  ScArray<BdAsset *> assets;
  ScArray<BdDefinition *> definitions;
};

// The loader's genuine third cdecl argument is unused at its sole direct caller; its context type remains unresolved.
struct BdAssetLoadContext;
void BdLoadAsset(const ScString *name, BdCatalog *catalog, BdAssetLoadContext *context);

// Original manager accessor; the manager supplies its recovered lazy decoration/config initialization.
BdCatalog *BdGetCatalog();
void BdCreateColors(BdCatalog *catalog);

#endif
