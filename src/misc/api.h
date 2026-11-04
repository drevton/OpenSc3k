#ifndef SC3_MISC_API_H
#define SC3_MISC_API_H

#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// Full native token transport phases; private nominal declarations remain unresolved.
u32 SC3_STDCALL CtExpandMiscTokens(ScString *text);
bool SC3_STDCALL CtSubstituteMiscToken(ScString *text, u32 *position);
bool SC3_STDCALL CtTryMiscToken(ScString *text, const ScString &token, u32 *position);

class CtCity;
class RsStream;
class ScStringView;

// Graph selection and current-value identities remain separate from matching layer and interface IDs.
enum ScGraphLabelId : u32 {
  kCtPowerGraphSelectionId = 0xe0afdf68,
  kCtPowerGraphValueId = 0xe0afdf69,
  kCtWaterGraphSelectionId = 0x2bf0033,
  kCtWaterGraphValueId = 0x2bf0034,
  kEcFundsGraphSelectionId = 0x630536cb,
  kEcFundsGraphValueId = 0xc30536d2,
  kNwTrafficGraphSelectionId = 0x29ca804,
  kNwTrafficGraphValueId = 0x29ca805,
  kCtCitySizeGraphSelectionId = 0x4308eadb,
  kCtCitySizeGraphValueId = 0x6308eae2,
  kCtNationalPopulationGraphSelectionId = 0x349a4cb,
  kCtNationalPopulationGraphValueId = 0xe349a51a,
  kCtCommerceGraphSelectionId = 0x8308ec75,
  kCtCommerceGraphValueId = 0xa308ec7c,
  kCtIndustryGraphSelectionId = 0xa308ec84,
  kCtIndustryGraphValueId = 0xe308ec8b,
  kCtUnemploymentGraphSelectionId = 0xa308ec96,
  kCtUnemploymentGraphValueId = 0x4308eca0,
  kEcPollutionGraphSelectionId = 0xe3079ef9,
  kEcPollutionGraphValueId = 0xe3079f00,
  kEcLandValueGraphSelectionId = 0xc3080bed,
  kEcLandValueGraphValueId = 0x43080bf4,
  kCtCrimeGraphSelectionId = 0x43092c78,
  kCtCrimeGraphValueId = 0x83092c7f,
  kCtMayorRatingGraphValueId = 0x259c040,
};

// Original tDataSet uses month0/year1/decade2 selectors.
enum class ScGraphDataSet : i32 { Month, Year, Decade };

// Shared E3012FC7 graph view, including installed slot104 before the history setters.
// Byte replies and borrowed pointer/reference spelling remain qualified. Slot104 uses only observed effects;
// its private nominal return remains unresolved. SetAutoMax retains Unlimited's raw byte carrier; Linux bool spelling does not prove a PC nominal declaration.
class ScGraphInfoView : public ScUnknown {
public:
  virtual const ScStringView *GetSelectionLabel() const = 0;
  virtual const ScStringView *GetCurrentValueLabel(ScGraphDataSet set) const = 0;
  virtual const u32 *GetSelectionLabelID() const = 0;
  virtual const u32 *GetValueLabelID() const = 0;
  virtual u32 GetLineColor() const = 0;
  virtual u32 GetMaxValue() const = 0;
  virtual u8 GetMonthData(u32 *out) = 0;
  virtual u8 GetMonthDataSet(u32 *out) const = 0;
  virtual u32 GetMonthAverage() const = 0;
  virtual u8 GetYearData(u32 *out) = 0;
  virtual u8 GetYearDataSet(u32 *out) const = 0;
  virtual u32 GetYearAverage() const = 0;
  virtual u8 GetDecadeData(u32 *out) = 0;
  virtual u8 GetDecadeDataSet(u32 *out) const = 0;
  virtual u32 GetDecadeAverage() const = 0;
  virtual u8 VerifyData() = 0;
  virtual u8 SetSelectionLabel(const ScStringView &value) = 0;
  virtual u8 SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) = 0;
  virtual u8 SetSelectionLabelID(const u32 &value) = 0;
  virtual u8 SetValueLabelID(const u32 &value) = 0;
  virtual u8 SetColor(u32 value) = 0;
  virtual u8 SetMaxValue(u32 value) = 0;
  virtual u8 SetAutoMax(u8 enabled) = 0;
  virtual void SetAutoMaxFactors(u32 minimum, f32 factor) = 0;
  virtual u8 SetMonthData(u32 value) = 0;
  virtual u8 SetMonthDataSet(u32 *values) = 0;
  virtual u8 SetYearData(u32 value) = 0;
  virtual u8 SetYearDataSet(u32 *values) = 0;
  virtual u8 SetDecadeData(u32 value) = 0;
  virtual u8 SetDecadeDataSet(u32 *values) = 0;
  virtual u8 SerialRead(CtCity *city, RsStream *record) = 0;
  virtual u8 SerialWrite(CtCity *city, RsStream *record) const = 0;
};

class WnResources;
class RsManager;
class ScCommandFactory;
class OcAttribCache;
class CtAgentTree;
class ScMessageServer;
class ScLanguageManager;

u32 ScParseMiscNumber(const char *text);
ScDirector *ScGetMiscDirector();
ApGameInterface *ScGetMiscApplication();
CtCity *ScGetMiscCity();
WnWindowManager *ScGetMiscWindowManager();
WnResources *ScGetMiscWindowResources();
RsManager *ScGetMiscResourceManager();
ScCommandFactory *ScGetMiscCommandFactory();
ScMessageServer *ScGetMiscMessageServer();
ScLanguageManager *ScGetMiscLanguageManager();
OcAttribCache *ScGetMiscAttribCache();
CtAgentTree *ScGetMiscAgentTree();

#endif
