#ifndef SC3_MISC_SC_GRAPH_INFO_H
#define SC3_MISC_SC_GRAPH_INFO_H

#include <deque>

#include "common/sc3_string.h"
#include "misc/api.h"
#include "misc/module.h"

class CtCity;
class RsStream;

// Actual cSC3GraphInfo receiver keeps the separate module lifetime and graph view beside its four Strings and queues.
// The original deque descriptors differ from modern STL layout; game histories retain their fixed logical lengths.
class ScGraphInfo : public ScMiscRefCount, public ScGraphInfoView {
public:
  explicit ScGraphInfo(u32 initial);
  ~ScGraphInfo() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Native configuration effects only; the original virtual SetAutoMaxFactors return remains unresolved.
  void SetAutoMaxFactors(u32 floor, f32 factor) override;
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
  // Native effects of33885; its ignored private nominal return remains unresolved.
  void ComputeAutoMax();
  ScString selectionLabel, currentLabels[3];
  u32 selectionLabelId, valueLabelId, color, maximum;
  u8 automaticScale;
  u32 minimum;
  f32 scaleFactor;
  std::deque<u32> monthly, yearly, decades;
};

#endif
