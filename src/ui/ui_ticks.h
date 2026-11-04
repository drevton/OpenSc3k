#ifndef SC3_UI_UI_TICKS_H
#define SC3_UI_UI_TICKS_H

#include <SDL3/SDL_timer.h>

#include "common/sc3.h"

// Return a wrapping millisecond counter for the original UI animation and hold intervals.
inline u32 UiTicks() {
  return static_cast<u32>(SDL_GetTicks());
}

#endif
