#ifndef SC3_BUILDINGS_ARCHITECT_DOCUMENT_H
#define SC3_BUILDINGS_ARCHITECT_DOCUMENT_H

#include "buildings/architect/bd_geometry.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"

struct BdFile {
  u32 id;
  ScString path;
};

class BdPart {
public:
  BdPart();
  BdPart(const BdPart &source);
  ~BdPart();

  u8 x, y;
  ScString name;
};

class BdComposition {
public:
  BdComposition();
  BdComposition(const BdComposition &source);
  ~BdComposition();

  ScString name;
  i32 height, width;
  ScArray<BdPart *> parts;
};

class BdGroup {
public:
  BdGroup();
  BdGroup(const BdGroup &source);
  ~BdGroup();

  ScString name;
  ScArray<BdComposition *> compositions;
};
void BD_STDCALL BdReadCompositionFile(BdComposition *composition, const ScString *path);

// A catalog definition owns its name and three variant names; assets refer to it by definition ID.
class BdDefinition {
public:
  BdDefinition();
  ~BdDefinition();
  u32 Release();

  u32 id;
  ScString name;
  BdPoint3 size;
  ScString variants[3];
  u32 variant_ids[3];
  u32 references;
};

// The registered124-byte PC document descriptor owns four strings and the metadata consumed by architect export.
// Offset names retain unresolved property meanings; setters retain observed register values pending source declarations.
class BdDocument final : public ScUnknown {
public:
  BdDocument();
  ~BdDocument();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetByte04();
  bool GetText08(ScStringView **out);
  bool GetText1C(ScStringView **out);
  bool GetText30(ScStringView **out);
  u32 GetWord44();
  bool GetWord54(u32 *out);
  bool GetText58(ScStringView **out);
  u8 SetByte04(u8 value);
  bool SetText08(const ScStringView *value);
  bool SetText1C(const ScStringView *value);
  bool SetText30(const ScStringView *value);
  u32 SetWord44(u32 value);
  u32 SetWord54(const u32 *value);
  bool GetWord4C(u32 *out);
  u32 SetWord4C(u32 value);
  bool GetBytes48(u8 *first, u8 *second, u8 *third, u8 *fourth);
  u8 SetBytes48(u8 first, u8 second, u8 third, u8 fourth);
  void SetText58(const ScStringView *value);

  u8 byte_004;
  ScString text_008, text_01c, text_030;
  u32 word_044;
  u8 bytes_048[4];
  u32 word_04c, references, word_054;
  ScString text_058;
  u32 words_06c[4];
};

#if defined(_WIN32)
#define BD_FASTCALL __fastcall
#else
#define BD_FASTCALL
#endif

class BdAsset;

struct BdMaterialSlot {
  BdMaterialSlot();
  ~BdMaterialSlot();

  u32 uses;
  BdAsset *value;
};

// The BAT's fixed material-index table. Cell payload bytes address these 256 game-owned slots.
class BdMaterials {
public:
  BdMaterials();
  ~BdMaterials();
  bool BD_FASTCALL Get(u32 index, BdAsset **value);
  BdAsset *BD_FASTCALL Peek(u32 index);
  u32 GetUse(u32 index, u32 *uses);
  // Complete slot-replacement effects; the private nominal return declaration remains unresolved.
  void Set(u32 index, BdAsset *value);
  bool Find(BdAsset *value, u32 *index);
  bool Insert(BdAsset *value, u32 *index);
  bool FindReplacement(u32 *index);
  void SetUse(u32 index, u32 uses);
  u32 BD_FASTCALL IncrementUse(u32 index);
  u32 BD_FASTCALL DecrementUse(u32 index);
  u32 AddRef();
  u32 Release();

  BdMaterialSlot slots[256];
  u32 references;
};

class BdAsset;
class BdDefinition;

// Four projected assets and their retained definitions accompany the object's grid dimensions.
class BdObjectInfo {
public:
  BdObjectInfo();
  ~BdObjectInfo();
  void GetAsset(i32 view, BdAsset **asset);

  BdAsset *assets[4];
  BdDefinition *definitions[4];
  BdPoint3 size;
};

bool BD_STDCALL BdGetObjectInfo(u32 id, BdObjectInfo *info);
bool BD_STDCALL BdFillObjectInfo(BdAsset *asset, BdObjectInfo *info);

#endif
