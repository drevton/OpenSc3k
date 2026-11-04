#ifndef SC3_WINDOW_SDL_INPUT_H
#define SC3_WINDOW_SDL_INPUT_H

#include "common/sc3.h"

union SDL_Event;
struct SDL_Cursor;
class ScStringView;

// Dispatch an SDL event through the existing game window manager and its keyboard state.
void WnDispatchSdlEvent(const SDL_Event *event);
bool WnSdlPostMessage(u32 type, uptr first, uptr second);
bool WnSdlPostedMessage(const SDL_Event *event, u32 *type, uptr *first, uptr *second);
bool WnSdlIsKeyDown(u32 key);
u32 WnSdlFlushInput();
SDL_Cursor *WnSdlLoadCursor(const ScStringView &path);
SDL_Cursor *WnSdlLoadCompositedCursor(const ScStringView &path);
void WnSdlDestroyCursor(SDL_Cursor *cursor);
void WnSdlSetCursor(SDL_Cursor *cursor);

#endif
