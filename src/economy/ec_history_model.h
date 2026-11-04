#ifndef SC3_ECONOMY_EC_HISTORY_MODEL_H
#define SC3_ECONOMY_EC_HISTORY_MODEL_H

#include <deque>

#include "common/sc3_string.h"
#include "misc/api.h"

class CtCity;
class RsStream;

// Recovered SIMECO history model, constructor168B2 and secondary table1001CB58.
// Its original STL deques retain12 monthly,10 annual and10 decadal values in newest-first order.
// Separate original primary lifetime table: deleting destructor0, Release4, AddRef8, RemoveRef12, count16.
// Its historical source class name is unresolved; it retains SIMECO rather than a foreign module director.
class EcHistoryLifetime {
public:
  EcHistoryLifetime();
  virtual ~EcHistoryLifetime();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Original lifetime primary0 and E3012FC7 graph view8 have distinct virtual tables.
class EcHistoryModel : public EcHistoryLifetime, public ScGraphInfoView {
public:
  explicit EcHistoryModel(u32 initial);
  ~EcHistoryModel() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Observed effects of slot104; its original nominal return remains unresolved.
  void SetAutoMaxFactors(u32 minimum, f32 factor) override;
  const ScStringView *GetSelectionLabel() const override;
  const ScStringView *GetCurrentValueLabel(ScGraphDataSet set) const override;
  const u32 *GetSelectionLabelID() const override;
  const u32 *GetValueLabelID() const override;
  u32 GetLineColor() const override;
  u32 GetMaxValue() const override;
  u8 GetMonthData(u32 *out) override;
  u8 GetMonthDataSet(u32 *out) const override;
  u32 GetMonthAverage() const override;
  u8 GetYearData(u32 *out) override;
  u8 GetYearDataSet(u32 *out) const override;
  u32 GetYearAverage() const override;
  u8 GetDecadeData(u32 *out) override;
  u8 GetDecadeDataSet(u32 *out) const override;
  u32 GetDecadeAverage() const override;
  u8 VerifyData() override;
  u8 SetSelectionLabel(const ScStringView &value) override;
  u8 SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) override;
  u8 SetSelectionLabelID(const u32 &value) override;
  u8 SetValueLabelID(const u32 &value) override;
  u8 SetColor(u32 value) override;
  u8 SetMaxValue(u32 value) override;
  u8 SetAutoMax(u8 enabled) override;
  u8 SetMonthData(u32 value) override;
  u8 SetMonthDataSet(u32 *values) override;
  u8 SetYearData(u32 value) override;
  u8 SetYearDataSet(u32 *values) override;
  u8 SetDecadeData(u32 value) override;
  u8 SetDecadeDataSet(u32 *values) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) const override;

protected:
  // Native effects of16BE1; the original private nominal return remains unresolved.
  void RecalculateLimit();
  ScString title, readouts[3];              // Original whole+0C,+20,+34,+48.
  u32 value_5c, value_60, value_64;         // Constructor initializes these metadata words to zero.
  u32 limit;                                // Whole+68, secondary+60.
  u8 automatic;                             // Whole+6C, secondary+64.
  u32 minimum;                              // Whole+70, secondary+68.
  f32 multiplier;                           // Whole+74, secondary+6C.
  std::deque<u32> monthly, annual, decadal; // Original whole+78,+A0,+C8 deques.
};

#endif
