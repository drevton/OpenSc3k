#ifndef SC3_SPRITES_SP_TICK_MANAGER_H
#define SC3_SPRITES_SP_TICK_MANAGER_H

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Original cISC3AnimationTickable prefix. Retail PC type46BE proves the two void methods and their output types.
class SpTickable : public ScUnknown {
public:
  virtual void DoAnimationTick(u32 elapsed) = 0;
  virtual void GetAnimationTickPriority(u8 *priority, i32 *interval) = 0;
};

// Reached cISC3AnimationTickManager operation; the animation receiver remains owned by the sprite module.
class SpTickManager : public ScUnknown {
public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual bool AddTickableObject(SpTickable *) = 0;
  virtual bool RemoveTickableObject(SpTickable *) = 0;
  virtual u8 GetAnimationSystemLoad() const = 0;
  virtual void DoAnimationTick() = 0;
  virtual u32 PauseAnimationSystem(bool pause) = 0; // SDK :34; installed +28.
  virtual bool IsAnimationSystemPaused() const = 0;
  virtual void StepAnimationSystem() = 0;
  virtual u32 GetCurrentTickTime() const = 0;
  virtual void SetNewPriority(SpTickable *unknown1, u8 unknown2, i32 unknown3) = 0;
  virtual void SetCriticalExecutionPriority(bool unknown1) = 0;
};

#endif
