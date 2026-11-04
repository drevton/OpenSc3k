#define _CRT_SECURE_NO_WARNINGS
#include "network/nw_rules.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "common/sc3_error.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"

// The rule loader leaves one byte of its bounded zeroed input buffer outside the maximum file read.
enum NwRuleInputLimit : u32 {
  kNwRuleInputBufferSize = 0x20000,
  kNwRuleMaximumInputRead = 0x1ffff,
};

NwPredicate::NwPredicate() : references(0) {
}

// Publish this iterator-test interface for its two actual IIDs, then invoke the live virtual AddRef. Unsupported IIDs
// clear the output.
bool NwPredicate::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwPredicate::AddRef() {
  return ++references;
}

u32 NwPredicate::Release() {
  --references;
  return references ? references : 1;
}

bool NwPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomNetwork);
}

NwPredicate::~NwPredicate() {
}

bool NwRoadPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomRoadNetwork);
}

bool NwRailPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomRailNetwork);
}

bool NwHighwayPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomHighwayNetwork);
}

bool NwSubwayPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomSubway);
}

bool NwPipePredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomPipe);
}

bool NwPowerPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomPowerNetwork);
}

bool NwTransitPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomTransitNetwork);
}

bool NwFloraPredicate::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kOcGeomFlora);
}

bool NwNotRoadPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomRoad);
}

bool NwNotRailPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomRail);
}

bool NwNotHighwayPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomHighway);
}

bool NwNotSubwayPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomSubway);
}

bool NwNotPipePredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomPipe);
}

bool NwNotPowerPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomPower);
}

bool NwNotNetworkPredicate::Test(ScOccupant *occupant) {
  return !occupant->IsGeomFlag(kOcGeomNetwork);
}

NwRuleCollection *NwFinalRules[6] = {};
NwRuleCollection *NwSimpleRules[6] = {};
NwRuleCollection *NwComplexRules[6] = {};

NwRuleTable::NwRuleTable() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

NwRuleTable::~NwRuleTable() {
  delete[] begin;
}

// Initialize all parser counters and pending owners, copy the name, then construct the real file utility.
NwRuleFile::NwRuleFile(const ScString &input)
    : state(0), expectedRules(0), expectedConditions(0), expectedResults(0), rulesRead(0), conditionsRead(0),
      resultsRead(0), pendingRule(nullptr), pendingGroup(nullptr), pendingRecord(nullptr), name(input),
      file(new RsFile(input)) {
}

// Delete the file before the pending rule, group and record owners, clearing each pointer in source order.
// Automatic member destruction then releases the name before the parsed rule table.
NwRuleFile::~NwRuleFile() {
  ScDelete(&file);
  ScDelete(&pendingRule);
  ScDelete(&pendingGroup);
  ScDelete(&pendingRecord);
}

NwRuleCollection::NwRuleCollection() {
}

// The shared trivial array releases pointer storage after those owned objects have been destroyed.
NwRuleCollection::~NwRuleCollection() {
  NwRuleFile **entry = files.data, **finish = files.size ? files.data + files.size : files.data;
  while (entry != finish) {
    delete *entry;
    *entry++ = nullptr;
  }
  files.Clear();
}

// Search files, selector rules and groups in stored order, stopping at the first successful result.
// Empty collections and empty files leave the neighborhood and prior result untouched.
u8 NwRuleCollection::Match(NwNeighborhood *neighborhood, NwRuleRecords *out) {
  NwRuleFile **entry = files.data, **finish = files.size ? files.data + files.size : files.data;
  for (; entry != finish; ++entry) {
    NwRule *rule = (*entry)->rules.begin, *end = (*entry)->rules.end;
    for (; rule != end; ++rule)
      if (rule->Match(neighborhood, out))
        return 1;
  }
  return 0;
}

// Assert atoi's defined decimal input domain, then use the original CRT conversion.
static i32 NwRuleNumber(const char *token) {
  u32 value = 0;
  for (const char *digit = token; *digit >= '0' && *digit <= '9'; ++digit) {
    u32 next = (u8)*digit - '0';
    assert(value <= ((u32)INT32_MAX - next) / 10);
    value = value * 10 + next;
  }
  return atoi(token);
}

// Append one complete selector rule to the owning file table using independent concrete group storage.
// Storage growth replaces only the original vector allocator, preserving all live rule values and their order.
static void NwAppendParsedRule(NwRuleTable *table, const NwRule &rule) {
  if (table->end == table->storageEnd) {
    usize count = table->begin != table->end ? (usize)(table->end - table->begin) : 0;
    usize capacity = count ? count * 2 : 4;
    if (capacity < count || capacity > SIZE_MAX / sizeof(NwRule))
      ScFatal(SC_ERROR_NOMEM, "Network rule table cannot grow.");
    NwRule *rules = new NwRule[capacity];
    for (usize i = 0; i < count; ++i)
      rules[i] = table->begin[i];
    delete[] table->begin;
    table->begin = rules;
    table->end = rules + count;
    table->storageEnd = rules + capacity;
  }
  *table->end++ = rule;
}

// Assertions bound the source CRT's defined argument domain without adding recoverable or fatal parser results.
u8 NwRuleFile::Load() {
  char bytes[kNwRuleInputBufferSize] = {};
  if (!file->Exists()) {
    file->Close();
    return 0;
  }
  file->Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead);
  u32 length = kNwRuleMaximumInputRead;
  if (!file->Read(bytes, &length)) {
    file->Close();
    return 0;
  }
  file->Close();
  pendingRule = new NwRule;
  pendingGroup = new NwRuleGroup;
  pendingRecord = new NwRuleRecord;
  for (char *token = strtok(bytes, " ,\n\t"); token; token = strtok(nullptr, " ,\n\t")) {
    i32 first = (i8)*token;
    assert(first >= -1);
    if (!isdigit(first))
      continue;
    i32 number = NwRuleNumber(token);
    switch (state) {
    case 0:
      switch (number) {
      case 0:
        state = 1;
        break;
      case 1:
        state = 2;
        break;
      case 2:
        state = 3;
        break;
      case 3:
        state = 4;
        break;
      case 4:
        state = 6;
        break;
      case 5:
        state = 7;
        break;
      }
      break;
    case 1:
      expectedRules = (u32)number;
      state = 0;
      break;
    case 2:
      pendingRule->selector = (u32)number;
      state = 0;
      break;
    case 3:
      expectedConditions = (u32)number;
      state = 0;
      break;
    case 4:
      pendingRecord->direction = (u8)number == 255 ? 31 : (u8)number;
      state = 5;
      break;
    case 5:
      pendingRecord->piece = (u32)(number >> 8);
      pendingRecord->state = (u8)number;
      *pendingGroup->conditions.records.Append() = *pendingRecord;
      pendingRecord->direction = 31;
      pendingRecord->piece = 0;
      pendingRecord->state = 0;
      ++conditionsRead;
      state = 0;
      break;
    case 6:
      expectedResults = (u32)number;
      state = 0;
      break;
    case 7:
      pendingRecord->direction = (u8)number == 255 ? 31 : (u8)number;
      state = 8;
      break;
    case 8:
      pendingRecord->piece = (u32)(number >> 8);
      pendingRecord->state = (u8)number;
      *pendingGroup->results.records.Append() = *pendingRecord;
      pendingRecord->direction = 31;
      pendingRecord->piece = 0;
      pendingRecord->state = 0;
      ++resultsRead;
      if (resultsRead == expectedResults) {
        pendingRule->begin = new NwRuleGroup[1];
        pendingRule->begin[0] = *pendingGroup;
        pendingRule->end = pendingRule->storageEnd = pendingRule->begin + 1;
        pendingGroup->conditions.records.Clear();
        pendingGroup->results.records.Clear();
        conditionsRead = resultsRead = 0;
        NwAppendParsedRule(&rules, *pendingRule);
        ++rulesRead;
        delete[] pendingRule->begin;
        pendingRule->begin = pendingRule->end = pendingRule->storageEnd = nullptr;
        pendingRule->selector = 0;
      }
      state = 0;
      break;
    }
  }
  return 1;
}

// Construct and load the named rule file, then retain ownership in the collection even when loading returns zero.
// The original entry reports one after publication and does not substitute a factory or parser status.
u8 NwRuleCollection::AddFile(const ScString &name) {
  NwRuleFile *file = new NwRuleFile(name);
  file->Load();
  *files.Append() = file;
  return 1;
}

static const u8 NwRuleDirections[25] = {
    10, 9, 8, 23, 22, 11, 4, 0, 7, 21, 12, 1, 31, 3, 20, 13, 5, 2, 6, 19, 14, 15, 16, 17, 18};
static const i8 NwRuleOffsets[32][2] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-2, 0},
    {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {2, -1}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 2},
    {-2, 2}, {-2, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

void NwRuleDirection(i8 dz, i8 dx, u8 *direction) {
  *direction = NwRuleDirections[u8(dz + 5 * dx + 12)];
}

void NwRuleOffset(u8 direction, i8 *dx, i8 *dz) {
  *dx = NwRuleOffsets[direction][0];
  *dz = NwRuleOffsets[direction][1];
}

void NwRuleDecode(i32 packed, u32 *piece, u8 *state) {
  *piece = u32(packed >> 8);
  *state = u8(packed);
}

NwRuleRecord::NwRuleRecord() : piece(0), direction(31), state(0) {
}

int NwRuleRecord::Matches(const NwRuleRecord &observed) {
  return direction == observed.direction && piece == observed.piece &&
      (state == observed.state || (!state && (!observed.state || observed.state == 5)));
}

// Search the full observation list independently for every condition, allowing repeated conditions to reuse a hit.
// An empty condition list succeeds without reading observations.
bool NwRuleRecords::Matches(const NwRuleRecords &observations) {
  for (u32 condition = 0; condition < records.size; ++condition) {
    bool found = false;
    for (u32 observed = 0; observed < observations.records.size && !found; ++observed)
      found = records.data[condition].Matches(observations.records.data[observed]) != 0;
    if (!found)
      return false;
  }
  return true;
}

NwNeighborhood::NwNeighborhood() : directionMask(0) {
}

// Copy concrete record values into native owning storage, preserving self-assignment and empty replacement.
static void NwCopyRuleRecords(NwRuleRecords *out, const NwRuleRecords &source) {
  if (out == &source)
    return;
  out->records.Clear();
  for (u32 i = 0; i < source.records.size; ++i)
    *out->records.Append() = source.records.data[i];
}

NwRuleGroup::NwRuleGroup() {
}

NwRuleGroup::NwRuleGroup(const NwRuleGroup &source) {
  NwCopyRuleRecords(&conditions, source.conditions);
  NwCopyRuleRecords(&results, source.results);
}

// Assign concrete group storage using the original two vector fields' value semantics.
// This native storage operation supports rule-array growth without recreating general vector machinery.
NwRuleGroup &NwRuleGroup::operator=(const NwRuleGroup &source) {
  NwCopyRuleRecords(&conditions, source.conditions);
  NwCopyRuleRecords(&results, source.results);
  return *this;
}

// Test conditions before changing the caller's result, then replace the complete result range on success.
// A successful empty group clears the old result; assigning the group's own result leaves it intact.
u8 NwRuleGroup::Match(const NwNeighborhood *neighborhood, NwRuleRecords *out) {
  if (conditions.records.size && !conditions.Matches(neighborhood->observations))
    return 0;
  NwCopyRuleRecords(out, results);
  return 1;
}

NwRule::NwRule() : selector(0), begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

// Copy the selector and all groups in source order, with independent condition and result ownership.
NwRule::NwRule(const NwRule &source) : selector(source.selector), begin(nullptr), end(nullptr), storageEnd(nullptr) {
  if (source.begin != source.end) {
    usize count = (usize)(source.end - source.begin);
    begin = new NwRuleGroup[count];
    end = storageEnd = begin + count;
    for (usize i = 0; i < count; ++i) {
      NwCopyRuleRecords(&begin[i].conditions, source.begin[i].conditions);
      NwCopyRuleRecords(&begin[i].results, source.begin[i].results);
    }
  }
}

NwRule::~NwRule() {
  delete[] begin;
}

// Replace native owning group storage with an independent copy of the source's live groups.
// This storage operation leaves game matching order and record values unchanged.
NwRule &NwRule::operator=(const NwRule &source) {
  if (this != &source) {
    usize count = source.begin != source.end ? (usize)(source.end - source.begin) : 0;
    NwRuleGroup *groups = count ? new NwRuleGroup[count] : nullptr;
    for (usize i = 0; i < count; ++i)
      groups[i] = source.begin[i];
    delete[] begin;
    selector = source.selector;
    begin = groups;
    end = storageEnd = count ? groups + count : groups;
  }
  return *this;
}

// Require an exact direction mask unless the selector is the wildcard 0x100.
// Scan groups in stored order and stop after the first successful result replacement.
u8 NwRule::Match(const NwNeighborhood *neighborhood, NwRuleRecords *out) {
  if (selector != 0x100 && selector != neighborhood->directionMask)
    return 0;
  for (NwRuleGroup *group = begin; group != end; ++group)
    if (group->Match(neighborhood, out))
      return 1;
  return 0;
}
