#ifndef SC3_MISC_ORDINANCES_H
#define SC3_MISC_ORDINANCES_H

#include <map>

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/sc3_string.h"
#include "misc/module.h"

class CtOrdinances;

// Actual monthly scheduler uses the module lifetime base and a borrowed ordinance owner; it takes no owner reference.
class CtOrdinanceManipulator : public CtLayerManipulator, public ScMiscRefCount {
public:
  CtOrdinanceManipulator(CtOrdinances *owner);
  ~CtOrdinanceManipulator() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

private:
  CtOrdinances *ordinances;
};

class CtCity;
class CtResidentialLayer;
class GeBuildingLayer;
class CtWorldLayer;

// Actual40-byte rule record owns its localized name. The two state bytes and signed cost bits remain unnormalized.
struct CtOrdinanceRule {
  u32 category;
  u8 on, available;
  u32 starting_year;
  ScString name;
  u32 monthly_cost, cost_divisor;
};

// Original120-byte receiver uses a map of owned rules. Modern native map storage is not an old tree-layout overlay.
// Canonical layer operations retain the original secondary receiver identity and explicit rule ownership.
class CtOrdinances : public CtOrdinanceLayer, public CtLayer {
public:
  CtOrdinances();
  virtual ~CtOrdinances();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Full monthly effects; the private nominal byte/Boolean return remains unresolved.
  u8 EndOfMonth();
  bool Shutdown() override;
  u32 GetLayerType() override;
  bool QueryOrdinanceOn(u32 id) override;
  bool QueryOrdinanceAvailable(u32 id) override;
  bool QueryOrdinanceAvailableAndOff(u32 id) override;
  u32 TotalOrdinancesOn() override;
  bool SetOrdinance(u32 id, bool on) override;
  bool MakeOrdinanceAvailable(u32 id) override;
  u32 GetOrdinanceListSize(u32 category) override;
  u32 GetOridnanceListSize(i32 category) override;
  bool GetOrdinanceList(CtOrdinance **out, u32 category) override;
  u32 GetOrdinanceCost(u32 id) override;
  u32 GetTotalMonthlyOrdinanceExpense() override;
  u32 GetTotalMonthlyOrdinanceIncome() override;
  // Original generic INI callback interprets its actual ordinance receiver.
  static void OrdinanceLayerAdd(const ScString &key, const ScString &value, void *context);

private:
  // Native effects of1A057; its private nominal return remains unresolved.
  void RefreshData();
  void MakeOldOrdinancesAvailable();
  bool CheckForNewOrdinances();
  void MakeAllOrdinancesAvailable();
  // Full native load effects; the private nominal return declaration remains unresolved.
  bool LoadOrdinanceINI();
  CtCity *city;
  CtResidentialLayer *residential;
  GeBuildingLayer *buildings;
  CtWorldLayer *world;
  u32 unemployment_threshold, building_threshold, commercial_threshold, aerospace_year;
  std::map<u32, CtOrdinanceRule *> rules;
  u32 day, year;
  i32 count_12360, count_12210, count_12230, count_12310, count_5000, count_13130, count_13010;
  u32 total_tracked;
  i32 count_14130, count_12851;
  u32 unemployment, monthly_income, monthly_expense;
  bool initialized;
  u32 references;
};

#endif
