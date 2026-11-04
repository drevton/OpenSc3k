#include "graphics/sdl_platform.h"

#include <SDL3/SDL.h>

#include <cstring>

#include "graphics/image.h"
#include "graphics/app_icon_png.inc"

static SDL_Window *GxSdlCanvas;
static SDL_Renderer *GxSdlRenderer;
static SDL_Texture *GxSdlTexture;
static u32 GxSdlWidth, GxSdlHeight;
static SDL_PixelFormat GxSdlFormat;
static u8 *GxSdlUploadedPixels;
static usize GxSdlUploadedCapacity;
static u32 *GxSdlPalettePixels;
static usize GxSdlPaletteCapacity;
static bool GxSdlUploaded;
static i32 GxSdlCursorX, GxSdlCursorY;
static const u8 *GxSdlXorPixels;
static u32 GxSdlXorWidth, GxSdlXorHeight;
static i32 GxSdlXorHotX, GxSdlXorHotY;
static bool GxSdlCursorVisible = true, GxSdlXorDrawn;
// Keep the queue mutex for process lifetime so a decoder finishing after window shutdown cannot use a freed lock.
static SDL_Mutex *GxSdlFrameMutex;
static u8 *GxSdlFramePixels;
static u32 GxSdlFrameWidth, GxSdlFrameHeight;
static bool GxSdlFrameOpen;

// Return the live canvas window as a borrowed pointer for the input layer.
SDL_Window *GxSdlWindow() {
  return GxSdlCanvas;
}

// Return the renderer owned by the live canvas without transferring ownership.
SDL_Renderer *GxSdlGetRenderer() {
  return GxSdlRenderer;
}

bool GxSdlWindowToGame(float windowX, float windowY, float *gameX, float *gameY) {
  return GxSdlRenderer && SDL_RenderCoordinatesFromWindow(GxSdlRenderer, windowX, windowY, gameX, gameY);
}

// Match SDL's cursor DPI policy rather than applying the display scale a second time on Windows.
// SDL's Windows backend scales custom cursors only when its cursor-DPI hint is enabled.
float GxSdlCursorScale() {
  if (!GxSdlCanvas)
    return 1;
#ifdef _WIN32
  if (!SDL_GetHintBoolean(SDL_HINT_MOUSE_DPI_SCALE_CURSORS, false))
    return 1;
  float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(GxSdlCanvas));
#else
  float scale = SDL_GetWindowDisplayScale(GxSdlCanvas);
#endif
  return scale > 0 ? scale : 1;
}

// Read the focused canvas pointer in logical framebuffer coordinates, retaining the last position after focus leaves.
void GxSdlCursorPosition(i32 *x, i32 *y) {
  if (GxSdlCanvas && SDL_GetMouseFocus() == GxSdlCanvas) {
    float windowX, windowY, gameX, gameY;
    SDL_GetMouseState(&windowX, &windowY);
    if (GxSdlWindowToGame(windowX, windowY, &gameX, &gameY)) {
      GxSdlCursorX = static_cast<i32>(gameX);
      GxSdlCursorY = static_cast<i32>(gameY);
    }
  }
  *x = GxSdlCursorX;
  *y = GxSdlCursorY;
}

// Create the decorated, resizable canvas and its renderer, retaining the SDL video subsystem until shutdown.
bool GxSdlCreateWindow(i32 width, i32 height, const char *caption) {
  if (GxSdlCanvas)
    return true;
  if (!GxSdlFrameMutex)
    GxSdlFrameMutex = SDL_CreateMutex();
  if (!GxSdlFrameMutex)
    return false;
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    return false;
  GxSdlCanvas = SDL_CreateWindow(caption ? caption : "SimCity 3000 Unlimited", width, height,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (GxSdlCanvas) {
    // The bundled icon also becomes the Dock icon on macOS when the executable runs outside an app bundle.
    if (SDL_IOStream *stream = SDL_IOFromConstMem(kGxAppIconPng, sizeof(kGxAppIconPng))) {
      if (SDL_Surface *icon = SDL_LoadPNG_IO(stream, true)) {
        SDL_SetWindowIcon(GxSdlCanvas, icon);
        SDL_DestroySurface(icon);
      }
    }
    GxSdlRenderer = SDL_CreateRenderer(GxSdlCanvas, nullptr);
  }
  if (!GxSdlRenderer ||
      !SDL_SetRenderLogicalPresentation(GxSdlRenderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX) ||
      !SDL_StartTextInput(GxSdlCanvas)) {
    GxSdlDestroyWindow();
    return false;
  }
  SDL_LockMutex(GxSdlFrameMutex);
  GxSdlFrameOpen = true;
  SDL_UnlockMutex(GxSdlFrameMutex);
  return true;
}

// Release the framebuffer texture, renderer and window before balancing video initialization.
void GxSdlDestroyWindow() {
  if (GxSdlFrameMutex) {
    SDL_LockMutex(GxSdlFrameMutex);
    GxSdlFrameOpen = false;
    delete[] GxSdlFramePixels;
    GxSdlFramePixels = nullptr;
    SDL_UnlockMutex(GxSdlFrameMutex);
  }
  if (GxSdlTexture)
    SDL_DestroyTexture(GxSdlTexture);
  if (GxSdlRenderer)
    SDL_DestroyRenderer(GxSdlRenderer);
  if (GxSdlCanvas)
    SDL_DestroyWindow(GxSdlCanvas);
  GxSdlTexture = nullptr;
  GxSdlRenderer = nullptr;
  GxSdlCanvas = nullptr;
  GxSdlWidth = GxSdlHeight = 0;
  delete[] GxSdlUploadedPixels;
  delete[] GxSdlPalettePixels;
  GxSdlUploadedPixels = nullptr;
  GxSdlPalettePixels = nullptr;
  GxSdlUploadedCapacity = GxSdlPaletteCapacity = 0;
  GxSdlUploaded = false;
  GxSdlCursorX = GxSdlCursorY = 0;
  GxSdlXorPixels = nullptr;
  GxSdlXorDrawn = false;
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool GxSdlSubmitFrame(const u8 *pixels, u32 pitch, u32 width, u32 height) {
  if (!GxSdlFrameMutex || !pixels || !width || !height || width > UINT32_MAX / 2)
    return false;
  u32 row = width * 2;
  if (pitch < row || u64(row) * height > SIZE_MAX || u64(pitch) * (height - 1) + row > SIZE_MAX)
    return false;
  u8 *copy = new u8[(usize)row * height];
  for (u32 y = 0; y < height; ++y)
    memcpy(copy + (usize)y * row, pixels + (usize)y * pitch, row);
  SDL_LockMutex(GxSdlFrameMutex);
  if (!GxSdlFrameOpen) {
    SDL_UnlockMutex(GxSdlFrameMutex);
    delete[] copy;
    return false;
  }
  delete[] GxSdlFramePixels;
  GxSdlFramePixels = copy;
  GxSdlFrameWidth = width;
  GxSdlFrameHeight = height;
  SDL_UnlockMutex(GxSdlFrameMutex);
  return true;
}

// Present the latest pending RGB565 frame on the main thread and release its private copy.
bool GxSdlDrainFrame() {
  if (!GxSdlFrameMutex)
    return false;
  SDL_LockMutex(GxSdlFrameMutex);
  u8 *pixels = GxSdlFramePixels;
  u32 width = GxSdlFrameWidth, height = GxSdlFrameHeight;
  GxSdlFramePixels = nullptr;
  SDL_UnlockMutex(GxSdlFrameMutex);
  if (!pixels)
    return false;
  bool result = GxSdlPresent(pixels, width * 2, width, height, 7, 16, nullptr);
  delete[] pixels;
  return result;
}

void GxSdlShowWindow() {
  if (GxSdlCanvas)
    SDL_ShowWindow(GxSdlCanvas);
}

bool GxSdlSetWindowFullscreen(bool fullscreen) {
  return GxSdlCanvas && SDL_SetWindowFullscreen(GxSdlCanvas, fullscreen);
}

void GxSdlCenterWindow() {
  if (GxSdlCanvas)
    SDL_SetWindowPosition(GxSdlCanvas, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void GxSdlSetTitle(const char *caption) {
  if (GxSdlCanvas)
    SDL_SetWindowTitle(GxSdlCanvas, caption);
}

// Show the native fallback outside the canvas and hide it while the XOR cursor is composed within the canvas.
void GxSdlSetCursorVisible(bool visible) {
  GxSdlCursorVisible = visible;
  if (GxSdlXorPixels || GxSdlXorDrawn)
    GxSdlRefreshCursor();
  else if (visible)
    SDL_ShowCursor();
  else
    SDL_HideCursor();
}

// Discard the device texture after a renderer reset and retire its upload state.
// The next presentation repopulates it from the complete game image, even when the CPU pixels are unchanged.
void GxSdlInvalidateTexture() {
  if (GxSdlTexture)
    SDL_DestroyTexture(GxSdlTexture);
  GxSdlTexture = nullptr;
  GxSdlUploaded = false;
}

// Compare visible pixels with the last successful upload, including raw drawing and palette-derived changes.
// Upload up to eight contiguous regions, then use their union when fragmented damage would require more calls.
static bool GxSdlUpload(const u8 *pixels, u32 pitch, u32 width, u32 height, u32 unit) {
  usize row = usize(width) * unit, size = row * height;
  if (size > GxSdlUploadedCapacity) {
    delete[] GxSdlUploadedPixels;
    GxSdlUploadedPixels = new u8[size];
    GxSdlUploadedCapacity = size;
    GxSdlUploaded = false;
  }
  SDL_Rect rectangles[8], bounds{};
  u32 count = 0;
  bool merged = false;
  if (!GxSdlUploaded) {
    rectangles[count++] = {0, 0, (int)width, (int)height};
  } else {
    for (u32 y = 0; y < height; ++y) {
      const u8 *input = pixels + usize(y) * pitch, *previous = GxSdlUploadedPixels + usize(y) * row;
      if (memcmp(input, previous, row) == 0)
        continue;
      usize first = 0, last = row;
      while (last - first >= 64 && memcmp(input + first, previous + first, 64) == 0)
        first += 64;
      while (last - first >= 64 && memcmp(input + last - 64, previous + last - 64, 64) == 0)
        last -= 64;
      SDL_Rect dirty{(int)(first / unit), (int)y, (int)((last - first) / unit), 1};
      if (!count)
        bounds = dirty;
      else {
        int right = bounds.x + bounds.w > dirty.x + dirty.w ? bounds.x + bounds.w : dirty.x + dirty.w;
        if (dirty.x < bounds.x)
          bounds.x = dirty.x;
        bounds.w = right - bounds.x;
        bounds.h = dirty.y + 1 - bounds.y;
      }
      if (merged)
        continue;
      if (count && rectangles[count - 1].x == dirty.x && rectangles[count - 1].w == dirty.w &&
          rectangles[count - 1].y + rectangles[count - 1].h == dirty.y)
        ++rectangles[count - 1].h;
      else if (count < 8)
        rectangles[count++] = dirty;
      else
        merged = true;
    }
  }
  if (merged) {
    rectangles[0] = bounds;
    count = 1;
  }
  for (u32 index = 0; index < count; ++index) {
    SDL_Rect area = rectangles[index];
    const u8 *input = pixels + usize(area.y) * pitch + usize(area.x) * unit;
    if (!SDL_UpdateTexture(GxSdlTexture, &area, input, (int)pitch)) {
      GxSdlUploaded = false;
      return false;
    }
    if (!area.x && area.w == (int)width && pitch == row)
      memcpy(GxSdlUploadedPixels + usize(area.y) * row, input, row * usize(area.h));
    else
      for (int y = 0; y < area.h; ++y)
        memcpy(GxSdlUploadedPixels + usize(area.y + y) * row + usize(area.x) * unit, input + usize(y) * pitch,
            usize(area.w) * unit);
  }
  GxSdlUploaded = true;
  return true;
}

// Composite AND/XOR pixels against the rendered canvas in physical output coordinates.
// SDL cannot invert desktop or window-chrome pixels, so this operation is clipped to the canvas.
static bool GxSdlDrawXorCursor() {
  GxSdlXorDrawn = false;
  if (!GxSdlXorPixels || !GxSdlCursorVisible || !GxSdlCanvas || SDL_GetMouseFocus() != GxSdlCanvas)
    return true;
  float wx, wy, gx, gy;
  SDL_GetMouseState(&wx, &wy);
  if (!GxSdlWindowToGame(wx, wy, &gx, &gy) || gx < 0 || gy < 0 || gx >= GxSdlWidth || gy >= GxSdlHeight)
    return true;
  int window_width, window_height, output_width, output_height;
  SDL_FRect presentation;
  if (!SDL_GetWindowSize(GxSdlCanvas, &window_width, &window_height) || window_width <= 0 || window_height <= 0 ||
      !SDL_GetRenderOutputSize(GxSdlRenderer, &output_width, &output_height) ||
      !SDL_GetRenderLogicalPresentationRect(GxSdlRenderer, &presentation))
    return false;
  float scale = GxSdlCursorScale();
  int cursor_width = static_cast<int>(GxSdlXorWidth * scale + 0.5f);
  int cursor_height = static_cast<int>(GxSdlXorHeight * scale + 0.5f);
  if (cursor_width <= 0 || cursor_height <= 0)
    return false;
  int left = static_cast<int>(SDL_floorf(wx * output_width / window_width - GxSdlXorHotX * scale + 0.5f));
  int top = static_cast<int>(SDL_floorf(wy * output_height / window_height - GxSdlXorHotY * scale + 0.5f));
  SDL_Rect cursor{left, top, cursor_width, cursor_height};
  SDL_Rect canvas{static_cast<int>(presentation.x + 0.5f), static_cast<int>(presentation.y + 0.5f),
      static_cast<int>(presentation.w + 0.5f), static_cast<int>(presentation.h + 0.5f)};
  SDL_Rect output{0, 0, output_width, output_height};
  SDL_Rect canvas_clip, area;
  if (!SDL_GetRectIntersection(&canvas, &output, &canvas_clip) ||
      !SDL_GetRectIntersection(&cursor, &canvas_clip, &area)) {
    // An external hotspot can place the entire cursor outside the canvas while its pointer remains inside it.
    GxSdlXorDrawn = true;
    return true;
  }
  SDL_Surface *captured = SDL_RenderReadPixels(GxSdlRenderer, &area);
  if (!captured)
    return false;
  SDL_Surface *surface = SDL_ConvertSurface(captured, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(captured);
  if (!surface)
    return false;
  if (surface->w != area.w || surface->h != area.h) {
    SDL_DestroySurface(surface);
    return false;
  }
  for (int y = 0; y < area.h; ++y) {
    u32 sy = static_cast<u32>(area.y + y - top) * GxSdlXorHeight / cursor_height;
    u8 *row = static_cast<u8 *>(surface->pixels) + usize(y) * surface->pitch;
    for (int x = 0; x < area.w; ++x) {
      u32 sx = static_cast<u32>(area.x + x - left) * GxSdlXorWidth / cursor_width;
      const u8 *source = GxSdlXorPixels + (usize(sy) * GxSdlXorWidth + sx) * 4;
      u8 *destination = row + x * 4;
      if (source[3])
        destination[0] = destination[1] = destination[2] = 0;
      destination[0] ^= source[0];
      destination[1] ^= source[1];
      destination[2] ^= source[2];
      destination[3] = 255;
    }
  }
  SDL_Texture *texture = SDL_CreateTextureFromSurface(GxSdlRenderer, surface);
  SDL_DestroySurface(surface);
  SDL_FRect destination{static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.w),
      static_cast<float>(area.h)};
  bool result = texture && SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) &&
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) &&
      SDL_SetRenderLogicalPresentation(GxSdlRenderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
      SDL_RenderTexture(GxSdlRenderer, texture, nullptr, &destination);
  bool restored = SDL_SetRenderLogicalPresentation(GxSdlRenderer, static_cast<int>(GxSdlWidth),
      static_cast<int>(GxSdlHeight), SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_DestroyTexture(texture);
  GxSdlXorDrawn = result && restored;
  return GxSdlXorDrawn;
}

// Refresh the retained frame on pointer movement, selection or visibility changes without uploading new game pixels.
void GxSdlRefreshCursor() {
  if (!GxSdlXorPixels && !GxSdlXorDrawn)
    return;
  if (!GxSdlRenderer || !GxSdlTexture || !GxSdlUploaded) {
    if (GxSdlCursorVisible)
      SDL_ShowCursor();
    else
      SDL_HideCursor();
    return;
  }
  bool hadOverlay = GxSdlXorDrawn;
  if (GxSdlXorPixels || hadOverlay) {
    if (SDL_RenderClear(GxSdlRenderer) && SDL_RenderTexture(GxSdlRenderer, GxSdlTexture, nullptr, nullptr) &&
        GxSdlDrawXorCursor())
      SDL_RenderPresent(GxSdlRenderer);
    else
      SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unable to refresh XOR cursor: %s", SDL_GetError());
  }
  if (GxSdlCursorVisible && !GxSdlXorDrawn)
    SDL_ShowCursor();
  else
    SDL_HideCursor();
}

// Borrow a selected cursor's AND/XOR pixels until it is deselected or destroyed.
void GxSdlSetXorCursor(const u8 *pixels, u32 width, u32 height, i32 hotspotX, i32 hotspotY) {
  GxSdlXorPixels = pixels;
  GxSdlXorWidth = width;
  GxSdlXorHeight = height;
  GxSdlXorHotX = hotspotX;
  GxSdlXorHotY = hotspotY;
  GxSdlRefreshCursor();
}

// Upload changed framebuffer regions at their actual pitch and present with nearest-neighbor logical scaling.
// Draw the retained texture even without damage so exposed and resized windows regain the complete image.
bool GxSdlPresent(const u8 *pixels, u32 pitch, u32 width, u32 height, i32 color, i32 depth, GxPaletteView *palette) {
  if (!GxSdlRenderer || !pixels || !width || !height)
    return false;
  SDL_PixelFormat format;
  if (depth == 8 && color == 3)
    format = SDL_PIXELFORMAT_ARGB8888;
  else if (depth == 16 && color == 5)
    format = SDL_PIXELFORMAT_XRGB1555;
  else if (depth == 16 && color == 7)
    format = SDL_PIXELFORMAT_RGB565;
  else if (depth == 32 && color == 14)
    format = SDL_PIXELFORMAT_ARGB8888;
  else
    return false;
  if (!GxSdlTexture || GxSdlWidth != width || GxSdlHeight != height || GxSdlFormat != format) {
    GxSdlUploaded = false;
    if (GxSdlTexture)
      SDL_DestroyTexture(GxSdlTexture);
    GxSdlTexture = SDL_CreateTexture(GxSdlRenderer, format, SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);
    if (!GxSdlTexture)
      return false;
    GxSdlWidth = width;
    GxSdlHeight = height;
    GxSdlFormat = format;
    if (!SDL_SetTextureScaleMode(GxSdlTexture, SDL_SCALEMODE_NEAREST) ||
        !SDL_SetRenderLogicalPresentation(GxSdlRenderer, (int)width, (int)height, SDL_LOGICAL_PRESENTATION_LETTERBOX))
      return false;
  }
  u32 *converted = nullptr;
  if (depth == 8) {
    if (!palette)
      return false;
    i32 paletteBits = palette->GetBitsPerPixel();
    if (paletteBits != 24 && paletteBits != 32)
      return false;
    u64 count = u64(width) * height;
    if (count > SIZE_MAX / sizeof(u32) || width > INT32_MAX / sizeof(u32))
      return false;
    if (count > GxSdlPaletteCapacity) {
      delete[] GxSdlPalettePixels;
      GxSdlPalettePixels = new u32[(usize)count];
      GxSdlPaletteCapacity = (usize)count;
    }
    converted = GxSdlPalettePixels;
    for (u32 y = 0; y < height; ++y)
      for (u32 x = 0; x < width; ++x) {
        u32 rgb;
        if (!palette->GetColor(pixels[(usize)y * pitch + x], &rgb))
          return false;
        converted[(usize)y * width + x] = kGxArgbAlphaMask | (rgb & kGxArgbRgbMask);
      }
    pixels = reinterpret_cast<const u8 *>(converted);
    pitch = width * sizeof(u32);
  }
  bool hadOverlay = GxSdlXorDrawn;
  bool result = GxSdlUpload(pixels, pitch, width, height, depth == 16 ? 2 : 4) && SDL_RenderClear(GxSdlRenderer) &&
      SDL_RenderTexture(GxSdlRenderer, GxSdlTexture, nullptr, nullptr) && GxSdlDrawXorCursor() &&
      SDL_RenderPresent(GxSdlRenderer);
  if (GxSdlXorPixels || hadOverlay) {
    if (GxSdlCursorVisible && !GxSdlXorDrawn)
      SDL_ShowCursor();
    else
      SDL_HideCursor();
  }
  if (!result)
    GxSdlUploaded = false;
  return result;
}
