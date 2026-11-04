#ifndef SC3_COMMON_CHEAT_MANAGER_H
#define SC3_COMMON_CHEAT_MANAGER_H

#include <list>
#include <set>
#include <vector>

#include "common/api.h"
#include "common/sc3_string.h"
#include "common/value.h"

// Original cGZCheatCodeManager collection elements from retail3195/31CD/3275.
struct ScCheatName {
  ScString text;
  u32 identifier;
};

struct ScCheatNotification {
  ScMessageTarget *target;
  u32 identifier;
};

struct ScCheatHistory {
  u32 identifier, date, reserved1, reserved2;
};

// Retail3397 fields329E/32ED/3329/3365 prove the original lists, ordered set and history vector.
// Targets are retained only by explicit notification registration; destroying the manager does not release them.
class ScCheatManager : public ScCheatCodeManager {
public:
  ScCheatManager();
  virtual ~ScCheatManager();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool RegisterCheatCode(u32 identifier, const ScStringView &name) override;
  bool UnregisterCheatCode(u32 identifier) override;
  bool AddNotification(ScMessageTarget *target, u32 identifier) override;
  bool RemoveNotification(ScMessageTarget *target, u32 identifier) override;
  bool GetCheatHasBeenSet(u32 identifier) override;
  bool SetCheatHasBeenSet(u32 identifier, bool enabled) override;
  u32 GetSetCheatCodes(u32 *out, u32 *count) override;
  bool SendCheatNotifications(const ScStringView &text, u32 identifier) override;
  i32 DoesCheatCodeMatch(const ScStringView &text, u32 *identifier) override;
  bool DoDefaultCheatCodeProcessing(const ScStringView &text, u32 *identifier) override;
  bool GetListOfAllCheatStrings(ScStringView **out, u32 *count) override;
  u32 GetCheatCodeOccurrenceCount() override;
  bool EnableCheatCodes(bool enabled, const ScStringView &password) override;
  bool GetCheatCodesEnabled() override;
  bool GetCheatCodePassword(u8 *out, u32 *length) override;
  bool SetCheatCodePassword(u8 *data, u32 length) override;
  bool SetEnableDisableStrings(const ScStringView &enable, const ScStringView &disable) override;
  void ClearAllSetCheatCodes();
  void AddCheatCodeHistoryRecord(u32 identifier, u32 date, u32 reserved1, u32 reserved2);
  u32 GetCheatCodeHistoryRecords(ScCheatHistory *out, u32 *count);

private:
  friend class ApApplication;
  std::list<ScCheatName> names;
  std::list<ScCheatNotification> notifications;
  std::set<u32> set_codes;
  std::vector<ScCheatHistory> history;
  u32 occurrences;
  u8 *password;
  bool enabled;
  ScString enable_text, disable_text;
};

#endif
