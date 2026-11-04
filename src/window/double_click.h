#ifndef SC3_WINDOW_DOUBLE_CLICK_H
#define SC3_WINDOW_DOUBLE_CLICK_H

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "common/sc3.h"

// Use SDL's configured interval and the same platform defaults SDL uses for double-click detection.
inline u32 WnDoubleClickTime() {
  const char *hint = SDL_GetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME);
  if (hint && *hint)
    return static_cast<u32>(SDL_atoi(hint));
#if defined(_WIN32)
  return GetDoubleClickTime();
#else
  return 500;
#endif
}

#endif
