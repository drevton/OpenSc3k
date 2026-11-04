#ifndef SC3_GRAPHICS_SDL_PLATFORM_H
#define SC3_GRAPHICS_SDL_PLATFORM_H

#include "common/sc3.h"

struct SDL_Window;
struct SDL_Renderer;
class GxPaletteView;

// Borrow the window used by the graphics canvas and convert SDL window coordinates to framebuffer coordinates.
SDL_Window *GxSdlWindow();
SDL_Renderer *GxSdlGetRenderer();
bool GxSdlWindowToGame(float windowX, float windowY, float *gameX, float *gameY);
void GxSdlCursorPosition(i32 *x, i32 *y);
float GxSdlCursorScale();

bool GxSdlCreateWindow(i32 width, i32 height, const char *caption);
void GxSdlDestroyWindow();
void GxSdlShowWindow();
bool GxSdlSetWindowFullscreen(bool fullscreen);
void GxSdlCenterWindow();
void GxSdlSetTitle(const char *caption);
void GxSdlSetCursorVisible(bool visible);
void GxSdlSetXorCursor(const u8 *pixels, u32 width, u32 height, i32 hotspotX, i32 hotspotY);
void GxSdlRefreshCursor();
void GxSdlInvalidateTexture();
bool GxSdlPresent(const u8 *pixels, u32 pitch, u32 width, u32 height, i32 color, i32 depth, GxPaletteView *palette);
bool GxSdlSubmitFrame(const u8 *pixels, u32 pitch, u32 width, u32 height);
bool GxSdlDrainFrame();

#endif
