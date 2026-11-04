#ifndef SC3_ECONOMY_EC_NEIGHBOR_GATE_H
#define SC3_ECONOMY_EC_NEIGHBOR_GATE_H

#include "economy/ec_civic.h"

// SIMRCI class 0356C518 uses the civic building views and validates its neighboring gate placement during simulation.
class EcNeighborGate : public EcCivicBuilding {
public:
  EcNeighborGate();
  ~EcNeighborGate() override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;

protected:
  u8 ReadRecord(CtCity *city, RsStream *record) override;
  u8 WriteRecord(CtCity *city, RsStream *record) override;

private:
  // Original return is incidental to the final occupant or terrain callback and is discarded by the caller.
  void ValidatePlacement();
};

#endif
