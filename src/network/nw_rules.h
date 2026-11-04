#ifndef SC3_NETWORK_NW_RULES_H
#define SC3_NETWORK_NW_RULES_H

#include "common/sc3_array.h"
#include "resources/file.h"

// Original table inputs use five-cell offsets and direction bytes0..31; validation belongs to actual callers.
void NwRuleDirection(i8 dz, i8 dx, u8 *direction);
void NwRuleOffset(u8 direction, i8 *dx, i8 *dz);
void NwRuleDecode(i32 packed, u32 *piece, u8 *state);

// Original packed six-byte entry retains the actual record stride without padding reads.
#pragma pack(push, 1)

struct NwRuleRecord {
  NwRuleRecord();
  int Matches(const NwRuleRecord &observed);
  u32 piece;
  u8 direction, state;
};

#pragma pack(pop)
static_assert(sizeof(NwRuleRecord) == 6);

// Concrete game records use the shared trivial-array storage instead of the original vector machinery.
struct NwRuleRecords {
  bool Matches(const NwRuleRecords &observations);
  ScArray<NwRuleRecord> records;
};

struct NwNeighborhood {
  NwNeighborhood();
  u32 directionMask;
  NwRuleRecords observations;
};

struct NwRuleGroup {
  NwRuleGroup();
  NwRuleGroup(const NwRuleGroup &source);
  NwRuleGroup &operator=(const NwRuleGroup &source);
  u8 Match(const NwNeighborhood *neighborhood, NwRuleRecords *out);
  NwRuleRecords conditions, results;
};

struct NwRule {
  NwRule();
  NwRule(const NwRule &source);
  NwRule &operator=(const NwRule &source);
  ~NwRule();
  u8 Match(const NwNeighborhood *neighborhood, NwRuleRecords *out);
  u32 selector;
  NwRuleGroup *begin, *end, *storageEnd;
};

// Type-specific rule storage replaces the original nested vector allocation while retaining its three pointer fields.
struct NwRuleTable {
  NwRuleTable();
  ~NwRuleTable();
  NwRule *begin, *end, *storageEnd;
};

class NwRuleFile {
public:
  explicit NwRuleFile(const ScString &name);
  virtual ~NwRuleFile();
  u8 Load();
  NwRuleTable rules;
  u8 state;
  u32 expectedRules, expectedConditions, expectedResults, rulesRead, conditionsRead, resultsRead;
  NwRule *pendingRule;
  NwRuleGroup *pendingGroup;
  NwRuleRecord *pendingRecord;
  ScString name;
  RsFile *file;
};

class NwRuleCollection {
public:
  NwRuleCollection();
  virtual ~NwRuleCollection();
  u8 Match(NwNeighborhood *neighborhood, NwRuleRecords *out);
  u8 AddFile(const ScString &name);
  ScArray<NwRuleFile *> files;
};

// Physical network order remains ROAD/HWAY/RAIL/SUBW/PIPE/POWR for each original module-global family.
extern NwRuleCollection *NwFinalRules[6];
extern NwRuleCollection *NwSimpleRules[6];
extern NwRuleCollection *NwComplexRules[6];

#endif
