#ifndef SC3_WINDOW_CURSOR_PLATFORM_H
#define SC3_WINDOW_CURSOR_PLATFORM_H

#include <SDL3/SDL_mouse.h>

#include "common/sc3_string.h"
#include "graphics/sc3_graphics.h"
#include "window/sdl_input.h"

// The SDL file-cursor path retains the game's software BMP implementation.
inline bool WnPlatformFileCursorHardware(bool) {
  return false;
}

// SDL-created cursor handles are owned by the caller and must be destroyed.
inline void WnPlatformDestroyCursor(void *cursor) {
  WnSdlDestroyCursor(static_cast<SDL_Cursor *>(cursor));
}

// Use canvas composition for inversion or a hotspot outside the image; ordinary CUR files retain the BMP path.
inline void *WnPlatformLoadCompositedCursor(const ScStringView &path) {
  return WnSdlLoadCompositedCursor(path);
}

// Remove any previously selected XOR overlay before returning to a game-drawn cursor.
inline void WnPlatformClearCursorOverlay() {
  WnSdlSetCursor(nullptr);
}

// Replace the owned SDL cursor before loading the requested CUR file.
inline void *WnPlatformLoadCursor(void *previous, const ScStringView &path) {
  if (previous)
    WnPlatformDestroyCursor(previous);
  return WnSdlLoadCursor(path);
}

// SDL cursor handles can be selected without querying graphics hardware support.
inline bool WnPlatformSelectsCursorDirectly() {
  return true;
}

// Clear the software cursor before selecting the SDL-owned platform handle.
inline void WnPlatformSetCursor(void *cursor, GxGraphics *graphics) {
  if (graphics)
    graphics->SetCursorBuffer(nullptr, 0, 0);
  WnSdlSetCursor(static_cast<SDL_Cursor *>(cursor));
}

#endif
