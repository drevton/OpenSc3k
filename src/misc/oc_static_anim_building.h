#ifndef SC3_MISC_OC_STATIC_ANIM_BUILDING_H
#define SC3_MISC_OC_STATIC_ANIM_BUILDING_H

#include "misc/oc_building.h"

// Original cSC3BuildingStaticAnim adds animation behavior without adding fields to the44-byte building.
class OcStaticAnimBuilding : public OcBuilding {
public:
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 delta) override;
};

#endif
