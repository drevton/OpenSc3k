#ifndef SC3_ECONOMY_EC_RCI_MODULE_H
#define SC3_ECONOMY_EC_RCI_MODULE_H

#include "common/sc3_director.h"
#include "economy/api.h"

// Concrete SIMRCI director owns the original ordered registration list.
class EcRciDirector : public ScDllDirector {
public:
  EcRciDirector();
  ~EcRciDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

// Unknown factories remain declaration-only actual dependencies; their private concrete product types are unresolved.
ScUnknown *EcCreateRciClass10036660();        // SIMRCI 0x10036660, class 0x409ff3ba.
ScUnknown *EcCreateRciCommercial();           // SIMRCI 0x1003669E, class 0x20ec9849.
ScUnknown *EcCreateRciIndustrial();           // SIMRCI 0x100366D0, class 0xa106cf3d.
ScUnknown *EcCreateRciClass10036702();        // SIMRCI 0x10036702, class 0x60a2966e.
ScUnknown *EcCreateRciResidential();          // SIMRCI 0x10036740, class 0xa106c520.
ScUnknown *EcCreateDemand();                  // SIMRCI 0x100367A7, class 0x60a42f32.
ScUnknown *EcCreateRciValve();                // SIMRCI 0x10036775, class 0xc0f1ec40.
ScUnknown *EcCreateRciResidentialDeveloper(); // SIMRCI 0x100367D9, class 0x619ba64e.
ScUnknown *EcCreateRciClass1003680E();        // SIMRCI 0x1003680E, class 0x41a3adc1.
ScUnknown *EcCreateRciClass10036843();        // SIMRCI 0x10036843, class 0xe1a53b30.
ScUnknown *EcCreateRciClass10036878();        // SIMRCI 0x10036878, class 0x82d2d72b.
ScUnknown *EcCreateRciClass100368AA();        // SIMRCI 0x100368AA, class 0xc1f81e7e.
ScUnknown *EcCreateRciClass100368DC();        // SIMRCI 0x100368DC, class 0x82348de5.
ScUnknown *EcCreateRciClass10036911();        // SIMRCI 0x10036911, class 0xa2c1eda3.
ScUnknown *EcCreateRciClass1003694C();        // SIMRCI 0x1003694C, class 0x61f44687.
ScUnknown *EcCreateRciClass10036987();        // SIMRCI 0x10036987, class 0xe22f4a83.
ScUnknown *EcCreateRciClass100369C2();        // SIMRCI 0x100369C2, class 0x630ea608.
ScUnknown *EcCreateRciBlockRule();            // SIMRCI 0x100369FD, class 0xa22653c0.
ScUnknown *EcCreateRciLotRule();              // SIMRCI 0x10036A38, class 0x222c9d00.
ScUnknown *EcCreateRciTransportBlockRule();   // SIMRCI 0x10036A73, class 0x822c9d13.
ScUnknown *EcCreateRciIndustrialBlockRule2(); // SIMRCI 0x10036AAE, class 0x422b55f2.
ScUnknown *EcCreateRciIndustrialBlockRule3(); // SIMRCI 0x10036AE9, class 0x822c9d24.
ScUnknown *EcCreateRciIndustrialBlockRule();  // SIMRCI 0x10036B24, class 0xc22c9d34.
ScUnknown *EcCreateRciCommercialLotRule2();   // SIMRCI 0x10036B5F, class 0x422c9d52.
ScUnknown *EcCreateRciCommercialLotRule3();   // SIMRCI 0x10036B9A, class 0x622c9d42.
ScUnknown *EcCreateRciCommercialBlockRule();  // SIMRCI 0x10036BD5, class 0x822c9d62.
ScUnknown *EcCreateRciClass10036C10();        // SIMRCI 0x10036C10, class 0xc2d3fd7e.
ScUnknown *EcCreateRciPortBlockRule();        // SIMRCI 0x10036C4B, class 0xe2d3fd92.
ScUnknown *EcCreateRciClass10036C86();        // SIMRCI 0x10036C86, class 0x2207d9ee.
ScUnknown *EcCreateRciClass10036CC1();        // SIMRCI 0x10036CC1, class 0x81ffc3ce.
ScUnknown *EcCreateRciClass10036CFC();        // SIMRCI 0x10036CFC, class 0x42e88384.
ScUnknown *EcCreateRciSchool();               // SIMRCI 0x10036D3A, class 0x612a8f75.
ScUnknown *EcCreateRciLibrary();              // SIMRCI 0x10036D75, class 0xe3319eb4.
ScUnknown *EcCreateRciMuseum();               // SIMRCI 0x10036DB0, class 0xa3319f62.
ScUnknown *EcCreateRciCollege();              // SIMRCI 0x10036DEB, class 0xc2ec529c.
ScUnknown *EcCreateRciHospital();             // SIMRCI 0x10036E26, class 0x2123cb83.
ScUnknown *EcCreateRciClass10036E61();        // SIMRCI 0x10036E61, class 0x0356c518.

class ScUnknown;
class ScDirector;
class ScFramework;
class ScCom;
class ApGameInterface;
class CtAgentTree;
class ScMessageServer;
class CtCity;
class OcAttribCache;
class RsManager;
class ScLanguageManager;
class WnResources;
class WnWindowManager;
class GxGraphics;
class RsFileSystem;

// Original SIMRCI36357 module getter; unrecovered registered factory bodies remain explicit dependencies.
ScDirector *EcGetRciDirector();
ScFramework *EcGetRciFramework();
ScCom *EcGetRciCom();
ApGameInterface *EcGetRciApplication();
CtAgentTree *EcGetRciAgentTree();
CtCity *EcGetRciCity();
OcAttribCache *EcGetRciAttribCache();
ScUnknown *EcCreateDemand();
ScMessageServer *EcGetRciMessageServer();
RsManager *EcGetRciResourceManager();
ScLanguageManager *EcGetRciLanguageManager();
WnResources *EcGetRciWindowResources();
WnWindowManager *EcGetRciWindowManager();
GxGraphics *EcGetRciGraphics();
RsFileSystem *EcGetRciFileSystem();

class CtResidentialLayer;
class EcValveLayer;

// SIMRCI links its own concrete valve behind the common public interface, with independent module references.
namespace EcRci {
class EcValve : public ::EcValve {
public:
  EcValve();
  virtual ~EcValve();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city, u32 identifier, CtLayer *listener) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) const override;
  u32 GetId() override;
  void SetId(u32 identifier) override;
  i32 QuerySupplyValue() override;
  i32 QueryDemandValue() override;
  i32 QueryValveValue() override;
  f32 QueryEconomyModifier() override;
  i32 QueryValveMax() override;
  i32 QueryValveMin() override;
  bool AddToSupplyValue(i32 amount) override;
  bool AddToDemandValue(i32 amount) override;
  bool SetSupplyValue(i32 amount) override;
  bool SetDemandValue(i32 amount) override;
  void SetValveMax(i32 maximum) override;
  void SetValveMin(i32 minimum) override;
  void SetInitialSupplyValue(i32 amount) override;
  void SetInitialDemandValue(i32 amount) override;
  f32 GetEconomyModifier() override;
  void SetEconomyModifier(f32 modifier) override;
  f32 GetTaxModifier() override;
  void SetTaxModifier(f32 modifier) override;
  u8 GetDemandCap() override;
  void SetDemandCap(u8 cap) override;
  bool EndOfMonth() override;
  void DebugLockValue(i32 value) override;
  void DebugUnlockValue() override;
  bool DebugIsValueLocked() override;

protected:
  virtual void UpdateValue(u8 smooth);
  // Notification is a direct module call; its private nominal return is not asserted.
  void NotifyValue() const;
  u32 references, identifier;                 // Original +04,+08.
  i32 supply, demand;                         // +0C,+10.
  u32 supply_accumulator, demand_accumulator; // +14,+18.
  i32 initial_supply, initial_demand;         // +1C,+20.
  i32 maximum, minimum, balance;              // +24,+28,+2C.
  bool flag_30, flag_31;                      // Original constructor initializes both true.
  f32 economy_modifier, tax_modifier;         // +34,+38.
  u8 demand_cap;                              // +3C.
  CtCity *city;                               // +40, borrowed until Shutdown.
};

// Residential supply also contributes working-age population to the city's workforce valve.
class EcResidentialValve : public EcValve {
public:
  EcResidentialValve();
  ~EcResidentialValve() override;
  bool Init(CtCity *city, u32 identifier, CtLayer *listener) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) const override;
  bool AddToSupplyValue(i32 amount) override;
  bool AddToDemandValue(i32 amount) override;
  bool EndOfMonth() override;

protected:
  EcValveLayer *valve_layer;
  CtResidentialLayer *residential_layer;
  ::EcValve *workforce_valve;
};
} // namespace EcRci

#endif
