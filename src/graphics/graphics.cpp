#define _CRT_SECURE_NO_WARNINGS
#include <cassert>
#include <cstdio>
#include <cstring>

#include "common/sc3_error.h"
#include "graphics/api.h"
#include "graphics/display_device.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"
#include "graphics/sc3_graphics_base.h"
#include "graphics/sdl_platform.h"
#include "window/api.h"

// Retain the original registered service priority and its startup/shutdown ordering.
enum GxEffectsPriority : i32 {
  kGxEffectsServicePriority = -1000000,
};

// The graphics service keeps its original priority within framework initialization and shutdown.
enum GxGraphicsPriority : i32 {
  kGxGraphicsServicePriority = 0x16e360,
};

// Borrowed live graphics interface supplied by the application module initialization.
GxGraphics *GxGraphicsCurrent = nullptr;

// The original void method rounds hue through its binary64 bias and writes it last.
void GxGraphicsBase::RGBToHSB(u8 red, u8 green, u8 blue, u8 *hue, u8 *saturation, u8 *value) {
  u8 maximum = red, minimum = red;
  if (green > maximum)
    maximum = green;
  if (blue > maximum)
    maximum = blue;
  if (green < minimum)
    minimum = green;
  if (blue < minimum)
    minimum = blue;
  *value = maximum;
  *saturation = maximum ? (u8)((maximum - minimum) / maximum) : 0;
  if (!*saturation) {
    *hue = 0;
    return;
  }
  f64 span = maximum - minimum, angle;
  if (red == maximum)
    angle = (green - blue) / span;
  else if (green == maximum)
    angle = (blue - red) / span + 2.0;
  else
    angle = (red - green) / span + 4.0;
  angle *= 60.0;
  if (angle < 0)
    angle += 360.0;
  f64 rounded = angle * 0.7083333134651184 + kScDoubleIntegerBias;
  u64 representation;
  memcpy(&representation, &rounded, sizeof(representation));
  *hue = (u8)representation;
}

// Round the sector through the hue output before scaling, retaining the unrounded scaled value for negative correction.
void GxGraphicsBase::RGBToHSB(u8 red, u8 green, u8 blue, f32 *hue, f32 *saturation, f32 *value) {
  u8 maximum = red, minimum = red;
  if (green > maximum)
    maximum = green;
  if (blue > maximum)
    maximum = blue;
  if (green < minimum)
    minimum = green;
  if (blue < minimum)
    minimum = blue;
  *value = maximum;
  *saturation = maximum ? (f32)(u8)(((f64)maximum - minimum) / maximum) : 0;
  if (*saturation == 0) {
    *hue = 0;
    return;
  }
  f64 span = maximum - minimum;
  if (red == maximum)
    *hue = (f32)((green - blue) / span);
  else if (green == maximum)
    *hue = (f32)((blue - red) / span + 2.0);
  else if (blue == maximum)
    *hue = (f32)((red - green) / span + 4.0);
  f64 angle = (f64)*hue * 60.0;
  *hue = (f32)angle;
  if (angle < 0)
    *hue = (f32)(angle + 360.0);
}

// Extract the low byte after the source's binary64 rounding bias, retaining wrap instead of channel clamping.
static u8 GxColorByte(f64 value) {
  f64 rounded = value + kScDoubleIntegerBias;
  u64 representation;
  memcpy(&representation, &rounded, sizeof(representation));
  return (u8)representation;
}

// Red and green always use float intermediates; sectors three and four retain the unspilled blue value.
void GxGraphicsBase::HSBToRGB(u8 hue, u8 saturation, u8 value, u8 *red, u8 *green, u8 *blue) {
  if (!saturation) {
    *red = value;
    *green = value;
    *blue = value;
    return;
  }
  f32 scaled = (f32)(hue * 0.0234375), amount = (f32)(saturation * (f64)0.0039215689f);
  i32 sector = (i32)scaled;
  f32 fraction = (f32)((f64)scaled - sector);
  f64 level = value * (f64)0.0039215689f, low = (1.0 - amount) * level;
  f32 falling = (f32)((1.0 - (f64)fraction * amount) * level);
  f32 rising = (f32)((1.0 - (1.0 - fraction) * amount) * level), r, g;
  f64 b;
  switch (sector) {
  case 0:
    r = (f32)level;
    g = rising;
    b = (f32)low;
    break;
  case 1:
    r = falling;
    g = (f32)level;
    b = (f32)low;
    break;
  case 2:
    r = (f32)low;
    g = (f32)level;
    b = rising;
    break;
  case 3:
    r = (f32)low;
    g = falling;
    b = level;
    break;
  case 4:
    r = rising;
    g = (f32)low;
    b = level;
    break;
  default:
    r = (f32)level;
    g = (f32)low;
    b = falling;
    break;
  }
  *red = GxColorByte((f64)r * 255.0);
  *green = GxColorByte((f64)g * 255.0);
  *blue = GxColorByte(b * 255.0);
}

// The zero-or-unordered saturation branch ignores hue; other paths retain the source's sector-dependent float spills.
void GxGraphicsBase::HSBToRGB(f32 hue, f32 saturation, f32 value, u8 *red, u8 *green, u8 *blue) {
  if (saturation == 0 || saturation != saturation) {
    u8 channel = GxColorByte((f64)value * 255.0);
    *red = channel;
    *green = channel;
    *blue = channel;
    return;
  }
  f64 scaled = (f64)hue * (f64)0.016666668f;
  assert(scaled >= -0x1p63 && scaled < 0x1p63);
  i32 sector = (i32)(u32)(u64)(i64)scaled;
  f32 fraction = (f32)(scaled - sector);
  f64 low = (1.0 - saturation) * value;
  f64 falling = (1.0 - (f64)fraction * saturation) * value;
  f32 rising = (f32)((1.0 - (1.0 - fraction) * saturation) * value), r, g;
  f64 b;
  switch (sector) {
  case 0:
    r = value;
    g = rising;
    b = low;
    break;
  case 1:
    r = (f32)falling;
    g = value;
    b = low;
    break;
  case 2:
    r = (f32)low;
    g = value;
    b = rising;
    break;
  case 3:
    r = (f32)low;
    g = (f32)falling;
    b = value;
    break;
  case 4:
    r = rising;
    g = (f32)low;
    b = value;
    break;
  default:
    r = value;
    g = (f32)low;
    b = (f32)falling;
    break;
  }
  *red = GxColorByte((f64)r * 255.0);
  *green = GxColorByte((f64)g * 255.0);
  *blue = GxColorByte(b * 255.0);
}

// Capture extrema before writing lightness, then saturation and hue; output aliases retain that store order.
void GxGraphicsBase::RGBToHLS(u8 red, u8 green, u8 blue, u8 *hue, u8 *lightness, u8 *saturation) {
  u8 maximum = red, minimum = red;
  if (green > maximum)
    maximum = green;
  if (blue > maximum)
    maximum = blue;
  if (green < minimum)
    minimum = green;
  if (blue < minimum)
    minimum = blue;
  i32 sum = maximum + minimum, span = maximum - minimum;
  u8 level = (u8)(sum / 2);
  *lightness = level;
  if (!span) {
    *saturation = 0;
    *hue = 0;
    return;
  }
  *saturation = (u8)(span / (level < 128 ? sum : 510 - sum));
  i32 sector;
  if (red == maximum)
    sector = (green - blue) / span;
  else if (green == maximum)
    sector = (blue - red) / span + 512;
  else
    sector = (red - green) / span + 1024;
  i32 angle = sector / 6;
  if (angle < 0)
    angle += 256;
  *hue = (u8)angle;
}

// Sector rounding passes through the hue output before degree scaling in the original void method.
void GxGraphicsBase::RGBToHLS(u8 red, u8 green, u8 blue, f32 *hue, f32 *lightness, f32 *saturation) {
  u8 maximum = red, minimum = red;
  if (green > maximum)
    maximum = green;
  if (blue > maximum)
    maximum = blue;
  if (green < minimum)
    minimum = green;
  if (blue < minimum)
    minimum = blue;
  f64 span = maximum - minimum, sum = maximum + minimum, level = sum * (1.0 / 510.0);
  *lightness = (f32)level;
  if (span == 0) {
    *saturation = 0;
    *hue = 0;
    return;
  }
  f64 divisor = level < 128.0 ? sum * 255.0 : 2.0 - sum * (1.0 / 255.0);
  *saturation = (f32)(span / divisor);
  if (red == maximum)
    *hue = (f32)((green - blue) / (span * 255.0));
  else if (green == maximum)
    *hue = (f32)((blue - red) / (span * 255.0) + 2.0);
  else if (blue == maximum)
    *hue = (f32)((red - green) / (span * 255.0) + 4.0);
  f64 angle = (f64)*hue * 60.0;
  *hue = (f32)angle;
  if (angle < 0)
    *hue = (f32)(angle + 360.0);
}

u32 GxHlsChannel(f64 low, f64 high, f64 hue) {
  if (hue > 360.0)
    hue -= 360.0;
  else if (!(hue >= 0))
    hue += 360.0;
  f64 channel;
  if (!(hue >= 60.0))
    channel = (high - low) * hue * (1.0 / 60.0) + low;
  else if (hue < 180.0)
    channel = high;
  else if (hue < 240.0)
    channel = (high - low) * (240.0 - hue) * (1.0 / 60.0) + low;
  else
    channel = low;
  f64 rounded = channel * 255.0 + kScDoubleIntegerBias;
  u64 representation;
  memcpy(&representation, &rounded, sizeof(representation));
  return (u32)representation + kScDoubleIntegerLowWordBias;
}

// Gray writes blue/green/red; colored conversion writes red/green/blue after computing the shared levels.
void GxGraphicsBase::HLSToRGB(u8 hue, u8 lightness, u8 saturation, u8 *red, u8 *green, u8 *blue) {
  if (!saturation) {
    *blue = lightness;
    *green = lightness;
    *red = lightness;
    return;
  }
  f64 level = lightness * (1.0 / 255.0), amount = saturation * (1.0 / 255.0);
  f64 high = level >= 0.5 ? amount + level - amount * level : (amount + 1.0) * level;
  f64 low = level + level - high, angle = hue * 1.40625;
  *red = (u8)GxHlsChannel(low, high, angle + 120.0);
  *green = (u8)GxHlsChannel(low, high, angle);
  *blue = (u8)GxHlsChannel(low, high, angle - 120.0);
}

// Zero or unordered saturation ignores hue; colored paths preserve the original double channel intermediates.
void GxGraphicsBase::HLSToRGB(f32 hue, f32 lightness, f32 saturation, u8 *red, u8 *green, u8 *blue) {
  if (saturation == 0 || saturation != saturation) {
    u8 channel = GxColorByte(lightness);
    *blue = channel;
    *green = channel;
    *red = channel;
    return;
  }
  f64 level = lightness, high;
  if (!(level >= 0.5))
    high = (saturation + 1.0) * level;
  else
    high = level + saturation - level * saturation;
  f64 low = level + level - high, angle = hue;
  *red = (u8)GxHlsChannel(low, high, angle + 120.0);
  *green = (u8)GxHlsChannel(low, high, angle);
  *blue = (u8)GxHlsChannel(low, high, angle - 120.0);
}

// Complement the three input channels into their outputs in red, green and blue order.
// The original void operation exposes only the ordered stores, including when outputs alias.
void GxGraphicsBase::RGBToCYM(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta) {
  *cyan = (u8)(255 - red);
  *yellow = (u8)(255 - green);
  *magenta = (u8)(255 - blue);
}

// Reverse the three channel complements through the same pooled source body as RGBToCYM.
// Preserve the separate original virtual operation and its ordered stores.
void GxGraphicsBase::CYMToRGB(u8 cyan, u8 yellow, u8 magenta, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255 - cyan);
  *green = (u8)(255 - yellow);
  *blue = (u8)(255 - magenta);
}

// Store channel complements, then subtract the original input minimum using the source's live output reloads.
// Capture the live black channel before the final store so aliased outputs retain the original ordered effects.
void GxGraphicsBase::RGBToCYMK(u8 red, u8 green, u8 blue, u8 *cyan, u8 *yellow, u8 *magenta, u8 *black) {
  u8 value = red;
  *cyan = (u8)(255 - red);
  *yellow = (u8)(255 - green);
  *magenta = (u8)(255 - blue);
  if (green < red)
    value = green;
  if (blue < value)
    value = blue;
  *black = value;
  *cyan = (u8)(*cyan - value);
  *yellow = (u8)(*yellow - *black);
  u8 minimum_value = *black;
  *magenta = (u8)(*magenta - minimum_value);
}

// Subtract each supplied channel and black from 255, preserving byte wrap and ordered output stores.
// The original void method retains these effects even when its three output pointers alias.
void GxGraphicsBase::CYMKToRGB(u8 cyan, u8 yellow, u8 magenta, u8 black, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255 - cyan - black);
  *green = (u8)(255 - yellow - black);
  *blue = (u8)(255 - magenta - black);
}

GxGraphicsBase::GxGraphicsBase() : GxSystemServiceBase(kGxGraphicsServiceId, kGxGraphicsServicePriority) {
}

// Publish the graphics identity before retaining; service/basic requests keep the original distinct base identities.
bool GxGraphicsBase::QueryInterface(u32 iid, void **out) {
  if (iid != IID_GxGraphics)
    return GxSystemServiceBase::QueryInterface(iid, out);
  *out = static_cast<GxGraphics *>(this);
  static_cast<GxGraphics *>(this)->AddRef();
  return true;
}

u32 GxGraphicsBase::AddRef() {
  return GxSystemServiceBase::AddRef();
}

u32 GxGraphicsBase::Release() {
  return GxSystemServiceBase::Release();
}

bool GxGraphicsBase::OnTick(u32) {
  return Draw();
}

bool GxGraphicsBase::PreFrameWorkInit() {
  return true;
}

bool GxGraphicsBase::PreAppInit() {
  return true;
}

bool GxGraphicsBase::PostAppInit() {
  return true;
}

bool GxGraphicsBase::PreAppShutdown() {
  return true;
}

bool GxGraphicsBase::PostAppShutdown() {
  return true;
}

bool GxGraphicsBase::PostSystemServiceShutdown() {
  return true;
}

bool GxGraphicsBase::OnInstall() {
  return true;
}

bool GxGraphicsBase::AddHook(GxFrameListener *listener) {
  return listeners.Add(listener);
}

bool GxGraphicsBase::RemoveHook(GxFrameListener *listener) {
  return listeners.Remove(listener);
}

u32 GxGraphicsBase::PauseDraw() {
  return ++pause;
}

u32 GxGraphicsBase::UnpauseDraw() {
  return --pause;
}

bool GxGraphicsBase::IsScreenPaletted() {
  GxColorType selected = GetScreenColorType();
  return selected.color == GxColorType::Color::Pal8 || selected.color == GxColorType::Color::Pal4;
}

u32 GxGraphicsBase::VideoMemoryFree(GxVideoMemoryType kind) {
  return VideoMemory(kind, 1);
}

u32 GxGraphicsBase::VideoMemoryTotal(GxVideoMemoryType kind) {
  return VideoMemory(kind, 0);
}

u8 GxGraphicsBase::IsForceSystemRAMSurfaces() {
  return no_hardware;
}

void GxGraphicsBase::SetForceSystemRAMSurfaces(bool value) {
  no_hardware = value;
}

// Store the borrowed cursor first, query dimensions through the captured input, then truncate all four words to16bits.
void GxGraphicsBase::SetCursorBuffer(GxBuffer *image, i32 x, i32 y) {
  cursor = image;
  GxSdlSetCursorVisible(!cursor && !suppress_overlay);
  if (!image)
    return;
  cursor_width = (u16)image->Width();
  cursor_height = (u16)image->Height();
  hotspot_x = (u16)x;
  hotspot_y = (u16)y;
}

void GxGraphicsBase::HookGSInit(GxBuffer *buffer) {
  listeners.GSInit(static_cast<GxGraphics *>(this), buffer);
}

void GxGraphicsBase::HookForceFullSystemRedraw() {
  listeners.ForceFullSystemRedraw();
}

void GxGraphicsBase::HookDraw() {
  listeners.Draw();
}

void GxGraphicsBase::HookProcessWindowMove() {
  listeners.ProcessWindowMove();
}

void GxGraphicsBase::HookCompleteSwitchToFullScreenMode() {
  listeners.CompleteSwitchToFullScreenMode();
}

void GxGraphicsBase::HookCompleteSwitchToWindowedMode() {
  listeners.CompleteSwitchToWindowedMode();
}

void GxGraphicsBase::HookRelease() {
  listeners.ReleaseAll();
}

// Decrement the suppression counter for visible cursors and increment it otherwise, retaining DWORD wrapping.
// Return the original signed LONG value without clamping; SDL synchronizes system-cursor visibility with this state.
i32 GxGraphicsBase::ShowCursor(bool visible) {
  if (visible)
    --suppress_overlay;
  else
    ++suppress_overlay;
  GxSdlSetCursorVisible(!cursor && !suppress_overlay);
  return suppress_overlay;
}

void GxGraphicsBase::TraceDriverInformation() {
}

i32 GxGraphicsBase::GetTotalSurfaceMemory() {
  return 0;
}

i32 GxGraphicsBase::GetTotalSystemSurfaceMemory() {
  return 0;
}

i32 GxGraphicsBase::GetTotalVideoSurfaceMemory() {
  return 0;
}

void GxGraphicsBase::IncrementTotalSystemSurfaceMemory(u32) {
}

void GxGraphicsBase::DecrementTotalSystemSurfaceMemory(u32) {
}

void GxGraphicsBase::IncrementTotalVideoSurfaceMemory(u32) {
}

void GxGraphicsBase::DecrementTotalVideoSurfaceMemory(u32) {
}

void GxGraphicsBase::MakeMemoryUsageString() {
}

// Return the original null system-palette interface; the release build creates no palette here.
GxPaletteView *GxGraphicsBase::SystemPalette() {
  return nullptr;
}

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
#include <cassert>

#include "common/api.h"
#include "graphics/module.h"
#include "graphics/sc3_effects.h"

u8 GxShade[1024], GxGamma[1024], GxBlend[32768];
u16 GxTwice[65536], GxHalf[65536];

// Build the shade table before reading the live color type, then initialize RGB555 or RGB565 tables.
// Unsupported formats retain the shade writes and return zero.
u8 GxInitAlphaSystem(const GxColorType &format) {
  static const f32 gamma[32] = {0.25f, 0.282f, 0.313f, 0.344f, 0.375f, 0.406f, 0.438f, 0.469f, 0.5f, 0.563f, 0.625f,
      0.688f, 0.75f, 0.875f, 0.9375f, 1.0f, 1.0f, 1.125f, 1.25f, 1.375f, 1.5f, 1.625f, 1.75f, 1.87f, 2.0f, 2.25f, 2.5f,
      2.75f, 3.0f, 3.33f, 3.67f, 4.0f};
  for (i32 channel = 0; channel < 32; ++channel) {
    for (i32 amount = 0; amount < 32; ++amount) {
      u8 value = (u8)(amount * (channel - 15) / 31 + 15);
      GxShade[32 * channel + amount] = value > 31 ? 31 : value;
    }
  }
  u32 code = (u32)format.color;
  if (code != 5 && code != 7)
    return false;
  for (u32 source = 0; source < 32; ++source) {
    for (u32 destination = 0; destination < 32; ++destination) {
      u32 accumulator = 32 * destination;
      for (u32 alpha = 0; alpha < 32; ++alpha) {
        u32 value = accumulator >> 5;
        GxBlend[1024 * source + 32 * destination + alpha] = (u8)(value >= 31 ? 31 : value);
        accumulator += source - destination;
      }
    }
  }
  for (u32 pixel = 0; pixel <= (code == 5 ? 0x7fffu : 0xffffu); ++pixel) {
    u32 red = 2 * (pixel >> (code == 5 ? 10 : 11)), green = 2 * ((pixel >> 5) & (code == 5 ? 31u : 63u));
    u32 blue = 2 * (pixel & 31), max_green = code == 5 ? 31 : 63;
    if (red > 31)
      red = 31;
    if (green > max_green)
      green = max_green;
    if (blue > 31)
      blue = 31;
    GxTwice[pixel] = (u16)(blue | (green << 5) | (red << (code == 5 ? 10 : 11)));
    GxHalf[pixel] = (u16)((pixel >> 1) & (code == 5 ? 0xbdefu : 0x7befu));
  }
  for (u32 channel = 0; channel < 32; ++channel) {
    for (u32 index = 0; index < 32; ++index) {
      u8 value = (u8)(i64)((f64)channel * gamma[index]);
      GxGamma[32 * channel + index] = value > 31 ? 31 : value;
    }
  }
  return true;
}

// Blend two RGB555 words through the actual shared lookup table. Alpha is consumed without clamping.
u32 GxBlend555(u32 source, u32 destination, u32 alpha) {
  u32 red = 1024 * (source >> 10) + 32 * (destination >> 10) + alpha;
  u32 green = 1024 * ((source >> 5) & 31) + 32 * ((destination >> 5) & 31) + alpha;
  u32 blue = 1024 * (source & 31) + 32 * (destination & 31) + alpha;
  assert(red < sizeof(GxBlend) && green < sizeof(GxBlend) && blue < sizeof(GxBlend));
  return GxBlend[blue] | (32u * (GxBlend[green] | (32u * GxBlend[red])));
}

// Blend RGB565 using the original five-bit green table index and six-bit output shift.
u32 GxBlend565(u32 source, u32 destination, u32 alpha) {
  u32 red = 1024 * (source >> 11) + 32 * (destination >> 11) + alpha;
  u32 green = 1024 * ((source >> 6) & 31) + 32 * ((destination >> 6) & 31) + alpha;
  u32 blue = 1024 * (source & 31) + 32 * (destination & 31) + alpha;
  assert(red < sizeof(GxBlend) && green < sizeof(GxBlend) && blue < sizeof(GxBlend));
  return GxBlend[blue] | (64u * (GxBlend[green] | (32u * GxBlend[red])));
}

static bool GxPositive(u32 value) {
  return value && !(value & kScIntegerSignBit);
}

// Clip a one-to-one rectangle pair against the destination buffer and optional explicit clip.
// Bounds are fetched separately for each axis, preserving the original virtual call order.
bool GxGenericBufferClip(GxBuffer *target, ScRect *source, ScRect *destination, const ScRect *clip) {
  u32 edge = (u32)destination->right;
  u32 delta = edge - (u32)target->GetBufferArea()->right;
  if (GxPositive(delta)) {
    destination->right = (i32)(edge - delta);
    source->right = (i32)((u32)source->right - delta);
    if (destination->right <= destination->left)
      return false;
  }
  delta = 0u - (u32)destination->left;
  if (GxPositive(delta)) {
    destination->left = (i32)((u32)destination->left + delta);
    source->left = (i32)((u32)source->left + delta);
    if (destination->left >= destination->right)
      return false;
  }
  edge = (u32)destination->bottom;
  delta = edge - (u32)target->GetBufferArea()->bottom;
  if (GxPositive(delta)) {
    destination->bottom = (i32)(edge - delta);
    source->bottom = (i32)((u32)source->bottom - delta);
    if (destination->bottom <= destination->top)
      return false;
  }
  delta = 0u - (u32)destination->top;
  if (GxPositive(delta)) {
    destination->top = (i32)((u32)destination->top + delta);
    source->top = (i32)((u32)source->top + delta);
    if (destination->top >= destination->bottom)
      return false;
  }
  if (clip) {
    delta = (u32)destination->right - (u32)clip->right;
    if (GxPositive(delta)) {
      destination->right = (i32)((u32)destination->right - delta);
      source->right = (i32)((u32)source->right - delta);
    }
    delta = (u32)clip->left - (u32)destination->left;
    if (GxPositive(delta)) {
      destination->left = (i32)((u32)destination->left + delta);
      source->left = (i32)((u32)source->left + delta);
    }
    delta = (u32)destination->bottom - (u32)clip->bottom;
    if (GxPositive(delta)) {
      destination->bottom = (i32)((u32)destination->bottom - delta);
      source->bottom = (i32)((u32)source->bottom - delta);
    }
    delta = (u32)clip->top - (u32)destination->top;
    if (GxPositive(delta)) {
      destination->top = (i32)((u32)destination->top + delta);
      source->top = (i32)((u32)source->top + delta);
    }
  }
  return destination->left < destination->right && destination->top < destination->bottom;
}

// Lock source, destination and mask in that order, blend the defined WORD pixels, then unlock in reverse order.
// Source lock failure preserves its raw byte; unsupported target depths/formats still finish the successful lock path.
u8 GxAlphaBltNoClip(
    GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &position) {
  const ScRect *source_rect = &source_input;
  u8 locked = source->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (destination->Lock(GxLockFlags::Bits)) {
    if (mask->Lock(GxLockFlags::Bits)) {
      if (destination->GetColorType().depth == 16) {
        u32 width = (u32)source_rect->right - (u32)source_rect->left;
        u32 source_pitch = source->GetColorSurfaceStride() >> 1, mask_pitch = mask->GetColorSurfaceStride();
        u32 destination_pitch = destination->GetColorSurfaceStride() >> 1;
        u8 *source_data = static_cast<u8 *>(source->GetColorSurfaceBits());
        source_data += 2 * ((i64)source_rect->top * source_pitch + source_rect->left);
        u8 *destination_data = static_cast<u8 *>(destination->GetColorSurfaceBits());
        destination_data += 2 * ((i64)position.y * destination_pitch + position.x);
        u32 rows = (u32)source_rect->bottom - (u32)source_rect->top;
        u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
        mask_data += (i64)source_rect->top * mask_pitch + source_rect->left;
        bool keyed = source->GetTransparency();
        u16 key = keyed ? (u16)source->GetBltFlags()->pixel : 0;
        i32 code = static_cast<i32>(destination->GetColorType().color);
        if (code != 5)
          code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
        if ((code == 5 || code == 7) && source_pitch) {
          for (u32 y = 0; y < rows; ++y) {
            for (u32 x = 0; x < width; ++x) {
              u16 pixel;
              memcpy(&pixel, source_data + 2 * (usize)x, 2);
              if (!keyed || pixel != key) {
                u16 previous;
                memcpy(&previous, destination_data + 2 * (usize)x, 2);
                u16 blended = (u16)(code == 5 ? GxBlend555(pixel, previous, mask_data[x])
                                              : GxBlend565(pixel, previous, mask_data[x]));
                memcpy(destination_data + 2 * (usize)x, &blended, 2);
              }
            }
            source_data += 2 * (usize)source_pitch;
            destination_data += 2 * (usize)destination_pitch;
            mask_data += mask_pitch;
          }
        }
      }
      mask->Unlock(GxLockFlags::Bits);
      result = 1;
    }
    destination->Unlock(GxLockFlags::Bits);
  }
  source->Unlock(GxLockFlags::Bits);
  return result;
}

// Copy the source extent and destination origin, then perform the actual clipped alpha composition.
u8 GxAlphaBlt(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
    const ScPoint &destination_point, const ScRect *clip) {
  ScRect sr = source_rect;
  ScRect dr = {destination_point.x, destination_point.y, (i32)((u32)destination_point.x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)destination_point.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint point{dr.left, dr.top};
  return GxAlphaBltNoClip(source, destination, mask, sr, point);
}

GxEffectsService::GxEffectsService() : GxSystemServiceBase(kGxBlendServiceId, kGxEffectsServicePriority) {
}

// Publish the effects identity before retaining the primary service; delegate other identities to the real base.
bool GxEffectsService::QueryInterface(u32 iid, void **out) {
  if (iid != IID_GxBlendService)
    return GxSystemServiceBase::QueryInterface(iid, out);
  *out = static_cast<GxBlendService *>(this);
  static_cast<ScSystemService *>(this)->AddRef();
  return true;
}

u32 GxEffectsService::AddRef() {
  return GxSystemServiceBase::AddRef();
}

u32 GxEffectsService::Release() {
  return GxSystemServiceBase::Release();
}

bool GxEffectsService::GenericBufferClip(GxBuffer *target, ScRect *source, ScRect *destination, const ScRect *clip) {
  return GxGenericBufferClip(target, source, destination, clip);
}

u8 GxEffectsService::InitAlphaSystem(const GxColorType &format) {
  return GxInitAlphaSystem(format);
}

u32 GxEffectsService::GetAlphaLevelCount() const {
  return 32;
}

// Read only the two dimensions used by the source's fixed-size information-header/palette allocation.
// Require the consumed width/height bytes without inventing a normal load-failure result; the original header leaks.
static void GxReadBitmapHeader(FILE *file, u32 *width, u32 *height, u32 *stride) {
  u8 *header = new u8[0x428];
  fseek(file, 14, SEEK_SET);
  [[maybe_unused]] usize received = fread(header, 1, 0x428, file);
  assert(received >= 12);
  *width = header[4] | ((u32)header[5] << 8) | ((u32)header[6] << 16) | ((u32)header[7] << 24);
  *height = header[8] | ((u32)header[9] << 8) | ((u32)header[10] << 16) | ((u32)header[11] << 24);
  *stride = ((*width - 1) | 3u) + 1;
}

// Read the original DWORD-sized payload allocation, retaining the actual byte count for later consumers.
// A short read is not itself invalid: the mask loader may never inspect the missing bytes.
static u8 *GxReadBitmapPixels(FILE *file, u32 height, u32 stride, u32 *received) {
  u32 count = stride * height;
  u8 *pixels = new u8[count];
  *received = static_cast<u32>(fread(pixels, 1, count, file));
  return pixels;
}

// Transfer bottom-up source rows after the original lock, data and pitch calls, without copying palette or row padding.
// Failed locks with a nonempty payload lead to an invalid original free rather than a defined game result.
static void GxTransferBitmapRows(GxBuffer *buffer, u8 *pixels, u32 width, u32 height, u32 stride,
    [[maybe_unused]] u32 received) {
  [[maybe_unused]] u32 count = stride * height;
  if (buffer->Lock(GxLockFlags::Bits)) {
    u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
    u32 pitch = buffer->GetColorSurfaceStride();
    u32 offset = stride * (height - 1);
    if (width)
      for (u32 row = 0; row < height; ++row) {
        assert(offset <= count && width <= count - offset);
        assert(offset <= received && width <= received - offset);
        memcpy(destination, pixels + offset, width);
        destination += pitch;
        offset -= stride;
      }
    buffer->Unlock(GxLockFlags::Bits);
  } else
    assert(!count && "original indexed bitmap frees the end pointer after a failed lock");
  delete[] pixels;
}

// Create an indexed buffer before opening the file, then divide every stored byte by the integer opacity divisor.
// Ignore factory/Init results and preserve failed-open buffer ownership, fixed header size and bottom-up transfer order.
GxBuffer *GxAlphaSurfaceFromFile(const char *filename, u32 levels) {
  GxBuffer *buffer = nullptr;
  GxGetDirector()->GZCOM()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
  FILE *file = fopen(filename, "rb");
  if (!file)
    return nullptr;
  u32 width, height, stride;
  GxReadBitmapHeader(file, &width, &height, &stride);
  buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(3)), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  u32 received;
  u8 *pixels = GxReadBitmapPixels(file, height, stride, &received);
  u32 count = stride * height;
  if (count) {
    assert(received == count && "original alpha bitmap transforms every allocated byte");
    assert(levels && levels <= 256);
    u32 divisor = 256 / levels;
    for (u32 index = 0; index < count; ++index)
      pixels[index] = (u8)(pixels[index] / divisor);
  }
  GxTransferBitmapRows(buffer, pixels, width, height, stride, received);
  fclose(file);
  return buffer;
}

// Load fixed-layout indexed bitmap rows directly, preserving the source factory-before-open and ignored Init result.
// The information-header palette is consumed but never attached to the returned buffer.
GxBuffer *GxMaskSurfaceFromFile(const char *filename) {
  GxBuffer *buffer = nullptr;
  GxGetDirector()->GZCOM()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
  FILE *file = fopen(filename, "rb");
  if (!file)
    return nullptr;
  u32 width, height, stride;
  GxReadBitmapHeader(file, &width, &height, &stride);
  buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(3)), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  u32 received;
  u8 *pixels = GxReadBitmapPixels(file, height, stride, &received);
  GxTransferBitmapRows(buffer, pixels, width, height, stride, received);
  fclose(file);
  return buffer;
}

// Forward the filename and opacity range to the original mask-file loader without reading service state.
GxBuffer *GxEffectsService::AlphaSurfaceFromFile(const char *filename, u32 levels) {
  return GxAlphaSurfaceFromFile(filename, levels);
}

// Forward the filename to the separate unscaled indexed-file loader and return its buffer identity directly.
GxBuffer *GxEffectsService::MaskSurfaceFromFile(const char *filename) {
  return GxMaskSurfaceFromFile(filename);
}

u8 GxEffectsService::AlphaBlt(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
    const ScPoint &target, const ScRect *clip) {
  return GxAlphaBlt(source, destination, mask, source_rect, target, clip);
}

u8 GxEffectsService::AlphaBltNoClip(
    GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) {
  return GxAlphaBltNoClip(source, destination, mask, source_rect, target);
}

u8 GxEffectsService::AlphaBltConstAlpha(GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect,
    const ScPoint &target, const ScRect *clip) {
  return GxAlphaBltConstAlpha(source, destination, opacity, source_rect, target, clip);
}

u8 GxEffectsService::AlphaBltConstAlphaNoClip(
    GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect, const ScPoint &target) {
  return GxAlphaBltConstAlphaNoClip(source, destination, opacity, source_rect, target);
}

u8 GxEffectsService::AlphaBltConstSource(u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
    const ScPoint &target, const ScRect *clip) {
  return GxAlphaBltConstSource(pixel, destination, mask, source_rect, target, clip);
}

u8 GxEffectsService::AlphaBltConstSourceNoClip(
    u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) {
  return GxAlphaBltConstSourceNoClip(pixel, destination, mask, source_rect, target);
}

u8 GxEffectsService::ShadeBltConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
    const ScPoint &target, u16 *table, const ScRect *clip) {
  return GxShadeBltConstAlpha(destination, mask, source_rect, target, table, clip);
}

u8 GxEffectsService::ShadeBltConstAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, u16 *table) {
  return GxShadeBltConstAlphaNoClip(destination, mask, source_rect, target, table);
}

u8 GxEffectsService::ShadowBlitConstAlpha(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxShadeBltConstAlpha(destination, mask, source_rect, target, GxHalf, clip);
}

u8 GxEffectsService::ShadowBlitConstAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) {
  return GxShadeBltConstAlphaNoClip(destination, mask, source_rect, target, GxHalf);
}

u8 GxEffectsService::BrightBlitConstAlpha(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxShadeBltConstAlpha(destination, mask, source_rect, target, GxTwice, clip);
}

u8 GxEffectsService::BrightBlitConstAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) {
  return GxShadeBltConstAlphaNoClip(destination, mask, source_rect, target, GxTwice);
}

u8 GxEffectsService::ShadeBlt(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxShadeBlt(destination, mask, source_rect, target, clip);
}

u8 GxEffectsService::ShadeBltNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) {
  return GxShadeBltNoClip(destination, mask, source_rect, target);
}

u8 GxEffectsService::ShadeBltStretch(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target, const ScRect *clip) {
  return GxShadeBltStretch(destination, mask, source_rect, target, clip);
}

u8 GxEffectsService::ShadeBltStretchNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target) {
  return GxShadeBltStretchNoClip(destination, mask, source_rect, target);
}

u8 GxEffectsService::ShadeBltWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity,
    const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxShadeBltWithAdditionalAlpha(destination, mask, opacity, source_rect, target, clip);
}

u8 GxEffectsService::ShadeBltWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, const ScPoint &target) {
  return GxShadeBltWithAdditionalAlphaNoClip(destination, mask, opacity, source_rect, target);
}

u8 GxEffectsService::ShadeBltStretchWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity,
    const ScRect &source_rect, const ScRect &target, const ScRect *clip) {
  return GxShadeBltStretchWithAdditionalAlpha(destination, mask, opacity, source_rect, target, clip);
}

u8 GxEffectsService::ShadeBltStretchWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, ScRect *target) {
  return GxShadeBltStretchWithAdditionalAlphaNoClip(destination, mask, opacity, source_rect, target);
}

u8 GxEffectsService::AlphaBltSelfAlpha(
    GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxAlphaBltSelfAlpha(source, destination, source_rect, target, clip);
}

u8 GxEffectsService::AlphaBltSelfAlphaNoClip(
    GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target) {
  return GxAlphaBltSelfAlphaNoClip(source, destination, source_rect, target);
}

u8 GxEffectsService::AlphaBltHalfAlphaVariableSource1ConstSource2(GxBuffer *source, GxBuffer *destination, u32 color,
    const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  return GxAlphaBltHalfAlphaVariableSource1ConstSource2(source, destination, color, source_rect, target, clip);
}

u8 GxEffectsService::AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(
    GxBuffer *source, GxBuffer *destination, u32 color, const ScRect &source_rect, const ScPoint &target) {
  return GxAlphaBltHalfAlphaVariableSource1ConstSource2NoClip(source, destination, color, source_rect, target);
}

// Blend WORD rows with the source's rounded uniform opacity and optional key after source/destination locks.
// Unsupported destination formats still complete the successful lock path; both unlock results are discarded.
u8 GxAlphaBltConstAlphaNoClip(
    GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect, const ScPoint &position) {
  const ScRect *sr = &source_rect;
  const ScPoint *dr = &position;
  u8 locked = source->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (destination->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)sr->right - (u32)sr->left;
      u32 source_pitch = source->GetColorSurfaceStride() >> 1,
          destination_pitch = destination->GetColorSurfaceStride() >> 1;
      f64 rounded = (f64)opacity * 32.0 + kScDoubleIntegerBias;
      u64 representation;
      memcpy(&representation, &rounded, sizeof(representation));
      u32 alpha = (u32)representation + kScDoubleIntegerLowWordBias;
      u8 *input = static_cast<u8 *>(source->GetColorSurfaceBits());
      input += 2 * ((iptr)sr->top * source_pitch + sr->left);
      u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
      output += 2 * ((iptr)dr->y * destination_pitch + dr->x);
      u32 height = (u32)sr->bottom - (u32)sr->top;
      bool keyed = source->GetTransparency();
      u16 key = keyed ? (u16)source->GetBltFlags()->pixel : 0;
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code && source_pitch) {
        for (u32 y = 0; y < height; ++y) {
          for (u32 x = 0; x < width; ++x) {
            u16 pixel;
            memcpy(&pixel, input + 2 * ((usize)y * source_pitch + x), 2);
            if (!keyed || pixel != key) {
              u8 *target = output + 2 * ((usize)y * destination_pitch + x);
              u16 previous;
              memcpy(&previous, target, 2);
              u16 blended = (u16)(code == 5 ? GxBlend555(pixel, previous, alpha) : GxBlend565(pixel, previous, alpha));
              memcpy(target, &blended, 2);
            }
          }
        }
      }
    }
    destination->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  source->Unlock(GxLockFlags::Bits);
  return result;
}

// Rebuild destination extent from the source rectangle, then clip before uniform-opacity composition.
u8 GxAlphaBltConstAlpha(GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect,
    const ScPoint &target, const ScRect *clip) {
  const ScPoint *destination_rect = &target;
  ScRect sr = source_rect;
  ScRect dr = {destination_rect->x, destination_rect->y, (i32)((u32)destination_rect->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)destination_rect->y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxAlphaBltConstAlphaNoClip(source, destination, opacity, sr, position);
}

// Blend a constant color through raw mask bytes after destination/mask locks and live format/pitch/data queries.
// Rounded WORD pitch controls row traversal; unsupported depths/formats still unlock both buffers and return one.
u8 GxAlphaBltConstSourceNoClip(
    u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target) {
  const ScRect *source = &source_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 target_gap = pitch - width, mask_gap = mask_pitch - width;
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target.x + pitch * (u32)target.y));
      uptr right = output + (i32)(2 * width);
      uptr end = output + (i32)(2 * pitch * ((u32)source->bottom - (u32)source->top));
      uptr input = (uptr)mask->GetColorSurfaceBits();
      input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code) {
        while (output < end) {
          while (output < right) {
            u16 previous;
            memcpy(&previous, (const void *)output, 2);
            u32 amount = *(const u8 *)input;
            u16 blended = (u16)(code == 5 ? GxBlend555(pixel, previous, amount) : GxBlend565(pixel, previous, amount));
            memcpy((void *)output, &blended, 2);
            output += 2;
            ++input;
          }
          input += (i32)mask_gap;
          output += (i32)(2 * target_gap);
          right += (i32)(2 * pitch);
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Derive the destination extent from the source size, clip local rectangles, then blend the selected constant color.
u8 GxAlphaBltConstSource(
    u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target, const ScRect *clip) {
  ScRect sr = source,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxAlphaBltConstSourceNoClip(pixel, destination, mask, sr, position);
}

// Replace each masked WORD through the caller's complete pixel lookup table; zero mask bytes leave pixels untouched.
// Only destination depth is queried, and the two unlock results do not replace the successful-lock return byte.
u8 GxShadeBltConstAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target, u16 *table) {
  const ScRect *source = &source_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 target_gap = pitch - width, mask_gap = mask_pitch - width;
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target.x + pitch * (u32)target.y));
      uptr right = output + (i32)(2 * width);
      uptr end = output + (i32)(2 * pitch * ((u32)source->bottom - (u32)source->top));
      uptr input = (uptr)mask->GetColorSurfaceBits();
      input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
      while (output < end) {
        while (output < right) {
          if (*(const u8 *)input) {
            u16 previous, replacement;
            memcpy(&previous, (const void *)output, 2);
            memcpy(&replacement, (const u8 *)table + 2 * (u32)previous, 2);
            memcpy((void *)output, &replacement, 2);
          }
          output += 2;
          ++input;
        }
        input += (i32)mask_gap;
        output += (i32)(2 * target_gap);
        right += (i32)(2 * pitch);
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Rebuild and clip local destination bounds before applying the mask-controlled pixel lookup table.
u8 GxShadeBltConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target,
    u16 *table, const ScRect *clip) {
  ScRect sr = source_input,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxShadeBltConstAlphaNoClip(destination, mask, sr, position, table);
}

// Apply the RGB555 gamma lookup through its ordinary cdecl entry, preserving the complete packed result.
u32 GxGamma555(u32 pixel, i32 amount) {
  u32 red = 32 * (pixel >> 10) + (u32)amount, green = 32 * ((pixel >> 5) & 31) + (u32)amount;
  u32 blue = 32 * (pixel & 31) + (u32)amount;
  assert(red < sizeof(GxGamma) && green < sizeof(GxGamma) && blue < sizeof(GxGamma));
  return GxGamma[blue] | (32u * (GxGamma[green] | (32u * GxGamma[red])));
}

// Apply the same RGB565 channel lookup as the register-convention entry without changing its green-channel shift.
u32 GxGamma565(u32 pixel, i32 amount) {
  u32 red = 32 * (pixel >> 11) + (u32)amount, green = 32 * ((pixel >> 6) & 31) + (u32)amount;
  u32 blue = 32 * (pixel & 31) + (u32)amount;
  assert(red < sizeof(GxGamma) && green < sizeof(GxGamma) && blue < sizeof(GxGamma));
  return GxGamma[blue] | (64u * (GxGamma[green] | (32u * GxGamma[red])));
}

// Preserve live query order, WORD-rounded pitch, physical end-pointer traversal and mask-then-target unlocks.
u8 GxShadeBltNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target) {
  const ScRect *source = &source_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 target_gap = pitch - width, mask_gap = mask_pitch - width;
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target.x + pitch * (u32)target.y));
      uptr right = output + (i32)(2 * width);
      uptr end = output + (i32)(2 * pitch * ((u32)source->bottom - (u32)source->top));
      uptr input = (uptr)mask->GetColorSurfaceBits();
      input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code) {
        while (output < end) {
          while (output < right) {
            i32 amount = *(const u8 *)input;
            if (amount != 15) {
              u16 previous;
              memcpy(&previous, (const void *)output, 2);
              u16 changed = (u16)(code == 5 ? GxGamma555(previous, amount) : GxGamma565(previous, amount));
              memcpy((void *)output, &changed, 2);
            }
            output += 2;
            ++input;
          }
          input += (i32)mask_gap;
          output += (i32)(2 * target_gap);
          right += (i32)(2 * pitch);
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Derive a one-to-one target extent and clip local copies before applying the gamma mask.
u8 GxShadeBlt(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target, const ScRect *clip) {
  ScRect sr = source_input,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxShadeBltNoClip(destination, mask, sr, position);
}

// Apply each selected gamma value to four WORDs only when target width is twice the mask-source width.
// Preserve the distinct555/565 neutral and written advances, repeated mask reads and unequal row/end advances.
u8 GxShadeBltStretchNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScRect &target_input) {
  const ScRect *source = &source_input, *target = &target_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)target->right - (u32)target->left;
      u32 source_width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 mask_gap = mask_pitch - source_width;
      if (width == 2 * source_width) {
        u32 row_bytes = 2 * pitch, target_gap = 2 * pitch - width, top = (u32)target->top;
        uptr output = (uptr)destination->GetColorSurfaceBits();
        output += (i32)(2 * ((u32)target->left + pitch * top));
        uptr right = output + (i32)(2 * width);
        uptr end = output + (i32)(2 * pitch * ((u32)target->bottom - top));
        uptr input = (uptr)mask->GetColorSurfaceBits();
        input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
        i32 code = static_cast<i32>(destination->GetColorType().color);
        if (code != 5)
          code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
        if (code) {
          while (output < end) {
            while (output < right) {
              if (*(const u8 *)input != 15) {
                for (u32 part = 0; part < 4; ++part) {
                  uptr at = output + (i32)((part >> 1) * row_bytes + 2 * (part & 1));
                  u16 previous;
                  memcpy(&previous, (const void *)at, 2);
                  i32 amount = *(const u8 *)input;
                  u16 changed = (u16)(code == 5 ? GxGamma555(previous, amount) : GxGamma565(previous, amount));
                  memcpy((void *)at, &changed, 2);
                }
                output += code == 5 ? 4 : 6;
              } else
                output += code == 5 ? 2 : 4;
              input += code == 5 ? 1 : 2;
            }
            input += (i32)mask_gap;
            output += (i32)(2 * target_gap);
            right += (i32)((code == 5 ? 4 : 2) * pitch);
          }
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Clip full source/target rectangles through the actual scaled clipper, then trim odd target extents before transfer.
u8 GxShadeBltStretch(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScRect &target_input, const ScRect *clip) {
  ScRect sr = source_input, dr = target_input;
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (((u32)dr.right - (u32)dr.left) & 1)
    dr.right = (i32)((u32)dr.right - 1);
  if (((u32)dr.bottom - (u32)dr.top) & 1)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  return GxShadeBltStretchNoClip(destination, mask, sr, dr);
}

// Preserve the low DWORD of the source's truncating64-bit conversion before its unsigned upper clamp.
// Assert the original integer-conversion domain without inventing a normal failure result or fatal game path.
static u32 GxShadeLevel(f32 opacity) {
  f64 value = (f64)opacity * 31.0;
  assert(value >= -0x1p63 && value < 0x1p63);
  u32 level = (u32)(i64)value;
  return level >= 32 ? 31 : level;
}

static void GxApplyGammaWord(uptr address, i32 code, i32 amount) {
  u16 previous;
  memcpy(&previous, (const void *)address, 2);
  u16 changed = (u16)(code == 5 ? GxGamma555(previous, amount) : GxGamma565(previous, amount));
  memcpy((void *)address, &changed, 2);
}

// Keep defined retained-index and mismatched-format tails, excluding only their original out-of-table gamma reads.
// This native boundary does not normalize the captured top coordinate or repair the565 corner's555 operation.
static void GxApplyGammaTail(uptr address, i32 code, i32 amount) {
  u16 pixel;
  memcpy(&pixel, (const void *)address, 2);
  assert(32u * (pixel >> (code == 5 ? 10 : 11)) + u32(amount) < sizeof(GxGamma) &&
      32u * ((pixel >> (code == 5 ? 5 : 6)) & 31) + u32(amount) < sizeof(GxGamma) &&
      32u * (pixel & 31) + u32(amount) < sizeof(GxGamma));
  u16 changed = (u16)(code == 5 ? GxGamma555(pixel, amount) : GxGamma565(pixel, amount));
  memcpy((void *)address, &changed, 2);
}

// Combine uniform opacity and each mask byte through the shade table before transforming destination WORDs.
u8 GxShadeBltWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_input, const ScPoint &target) {
  const ScRect *source = &source_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 target_gap = pitch - width, mask_gap = mask_pitch - width, level = GxShadeLevel(opacity);
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target.x + pitch * (u32)target.y));
      uptr right = output + (i32)(2 * width);
      uptr end = output + (i32)(2 * pitch * ((u32)source->bottom - (u32)source->top));
      uptr input = (uptr)mask->GetColorSurfaceBits();
      input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code) {
        while (output < end) {
          while (output < right) {
            i32 amount = GxShade[32 * (u32) * (const u8 *)input + level];
            if (amount != 15)
              GxApplyGammaWord(output, code, amount);
            output += 2;
            ++input;
          }
          input += (i32)mask_gap;
          output += (i32)(2 * target_gap);
          right += (i32)(2 * pitch);
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Derive and clip a local one-to-one destination extent before applying opacity-modulated gamma-mask values.
u8 GxShadeBltWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_input,
    const ScPoint &target, const ScRect *clip) {
  ScRect sr = source_input,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxShadeBltWithAdditionalAlphaNoClip(destination, mask, opacity, sr, position);
}

// Trim the actual target rectangle's odd extents after both locks, then apply doubled shade-table gamma samples.
// Retain each format's row-end advance and odd-tail defects, including the565 final corner's555 lookup.
// Before any sample, the retained shade value is the captured target top, including zero-source-width odd columns.
u8 GxShadeBltStretchWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_input, ScRect *target) {
  const ScRect *source = &source_input;
  u8 locked = destination->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (mask->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 left = (u32)target->left;
      bool odd_width = ((u32)target->right - left) & 1;
      if (odd_width)
        target->right = (i32)((u32)target->right - 1);
      bool odd_height = ((u32)target->bottom - (u32)target->top) & 1;
      if (odd_height)
        target->bottom = (i32)((u32)target->bottom - 1);
      u32 width = (u32)target->right - left, source_width = (u32)source->right - (u32)source->left;
      u32 mask_pitch = mask->GetColorSurfaceStride(), pitch = destination->GetColorSurfaceStride() >> 1;
      u32 mask_gap = mask_pitch - source_width, level = GxShadeLevel(opacity);
      if (width == 2 * source_width) {
        u32 target_gap = 2 * pitch - width, top = (u32)target->top;
        uptr output = (uptr)destination->GetColorSurfaceBits();
        output += (i32)(2 * ((u32)target->left + pitch * top));
        uptr right = output + (i32)(2 * width);
        uptr end = output + (i32)(2 * pitch * ((u32)target->bottom - top));
        uptr input = (uptr)mask->GetColorSurfaceBits();
        input += (i32)((u32)source->left + mask_pitch * (u32)source->top);
        i32 code = static_cast<i32>(destination->GetColorType().color);
        if (code != 5)
          code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
        i32 amount = (i32)top;
        if (code) {
          while (output < end) {
            bool extra_row = odd_height && output + (i32)(4 * pitch) >= end;
            while (output < right) {
              amount = GxShade[32 * (u32) * (const u8 *)input + level];
              if (amount != 15)
                for (u32 part = 0; part < 4; ++part)
                  GxApplyGammaWord(output + (i32)(2 * ((part >> 1) * pitch + (part & 1))), code, amount);
              output += 4;
              ++input;
            }
            if (odd_width) {
              GxApplyGammaTail(output, code, amount);
              if (code == 5)
                GxApplyGammaTail(output + (i32)(2 * pitch), code, amount);
            }
            if (extra_row) {
              output = end;
              right += (i32)(2 * pitch);
              input -= source_width;
              while (output < right) {
                amount = GxShade[32 * (u32) * (const u8 *)input + level];
                if (amount != 15) {
                  GxApplyGammaWord(output, code, amount);
                  GxApplyGammaWord(output + 2, code, amount);
                }
                output += 4;
                ++input;
              }
              if (odd_width)
                GxApplyGammaTail(output, 5, amount);
            }
            input += (i32)mask_gap;
            output += (i32)(2 * target_gap);
            right += (i32)((code == 5 ? 4 : 2) * pitch);
          }
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  destination->Unlock(GxLockFlags::Bits);
  return result;
}

// Scaled clipping changes local copies only; the core subsequently performs its own odd-edge trimming on that target.
u8 GxShadeBltStretchWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_input,
    const ScRect &target, const ScRect *clip) {
  ScRect sr = source_input, dr = target;
  return destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr)
      ? GxShadeBltStretchWithAdditionalAlphaNoClip(destination, mask, opacity, sr, &dr)
      : 0;
}

// Blend live source WORDs using their own low five bits, skipping the source key only when its virtual flag enables it.
// Preserve source-then-target locks, target-only depth gating, physical source-row ends and reverse ignored unlocks.
u8 GxAlphaBltSelfAlphaNoClip(
    GxBuffer *source, GxBuffer *destination, const ScRect &source_input, const ScPoint &target) {
  const ScRect *source_rect = &source_input;
  u8 locked = source->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (destination->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source_rect->right - (u32)source_rect->left;
      u32 source_pitch = source->GetColorSurfaceStride() >> 1, pitch = destination->GetColorSurfaceStride() >> 1;
      u32 source_gap = source_pitch - width, target_gap = pitch - width;
      uptr input = (uptr)source->GetColorSurfaceBits();
      input += (i32)(2 * ((u32)source_rect->left + source_pitch * (u32)source_rect->top));
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target.x + pitch * (u32)target.y));
      uptr right = input + (i32)(2 * width);
      uptr end = input + (i32)(2 * source_pitch * ((u32)source_rect->bottom - (u32)source_rect->top));
      bool keyed = source->GetTransparency();
      u16 key = keyed ? (u16)source->GetBltFlags()->pixel : 0;
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code) {
        while (input < end) {
          while (input < right) {
            u16 pixel;
            memcpy(&pixel, (const void *)input, 2);
            if (!keyed || pixel != key) {
              u16 previous;
              memcpy(&previous, (const void *)output, 2);
              u16 blended =
                  (u16)(code == 5 ? GxBlend555(pixel, previous, pixel & 31) : GxBlend565(pixel, previous, pixel & 31));
              memcpy((void *)output, &blended, 2);
            }
            input += 2;
            output += 2;
          }
          input += (i32)(2 * source_gap);
          output += (i32)(2 * target_gap);
          right += (i32)(2 * source_pitch);
        }
      }
    }
    destination->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  source->Unlock(GxLockFlags::Bits);
  return result;
}

// Derive destination extents from source size and clip local rectangles before source-pixel-alpha composition.
u8 GxAlphaBltSelfAlpha(
    GxBuffer *source, GxBuffer *destination, const ScRect &source_input, const ScPoint &target, const ScRect *clip) {
  ScRect sr = source_input,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint position{dr.left, dr.top};
  return GxAlphaBltSelfAlphaNoClip(source, destination, sr, position);
}

// Lock both buffers, then clear destination WORDs for selected source pixels in the supported sixteen-bit formats.
// The Windows implementation never reads the original unsigned color argument.
u8 GxAlphaBltHalfAlphaVariableSource1ConstSource2NoClip(
    GxBuffer *source, GxBuffer *destination, u32, const ScRect &source_input, const ScPoint &target_input) {
  const ScRect *source_rect = &source_input;
  const ScPoint *target = &target_input;
  u8 locked = source->Lock(GxLockFlags::Bits);
  if (!locked)
    return locked;
  u8 result = 0;
  if (destination->Lock(GxLockFlags::Bits)) {
    if (destination->GetColorType().depth == 16) {
      u32 width = (u32)source_rect->right - (u32)source_rect->left;
      u32 source_pitch = source->GetColorSurfaceStride() >> 1, pitch = destination->GetColorSurfaceStride() >> 1;
      u32 source_gap = source_pitch - width, target_gap = pitch - width;
      uptr input = (uptr)source->GetColorSurfaceBits();
      input += (i32)(2 * ((u32)source_rect->left + source_pitch * (u32)source_rect->top));
      uptr output = (uptr)destination->GetColorSurfaceBits();
      output += (i32)(2 * ((u32)target->x + pitch * (u32)target->y));
      uptr right = input + (i32)(2 * width);
      uptr end = input + (i32)(2 * source_pitch * ((u32)source_rect->bottom - (u32)source_rect->top));
      bool keyed = source->GetTransparency();
      u16 key = keyed ? (u16)source->GetBltFlags()->pixel : 0;
      i32 code = static_cast<i32>(destination->GetColorType().color);
      if (code != 5)
        code = static_cast<i32>(destination->GetColorType().color) == 7 ? 7 : 0;
      if (code) {
        while (input < end) {
          if (keyed) {
            while (input < right) {
              u16 pixel;
              memcpy(&pixel, (const void *)input, 2);
              if (pixel != key) {
                u16 zero = 0;
                memcpy((void *)output, &zero, 2);
              }
              input += 2;
              output += 2;
            }
          } else if (input < right) {
            usize bytes = 2 * ((right - input + 1) >> 1);
            memset((void *)output, 0, bytes);
            input += bytes;
            output += bytes;
          }
          input += (i32)(2 * source_gap);
          output += (i32)(2 * target_gap);
          right += (i32)(2 * source_pitch);
        }
      }
    }
    destination->Unlock(GxLockFlags::Bits);
    result = 1;
  }
  source->Unlock(GxLockFlags::Bits);
  return result;
}

// Capture the source rectangle, derive its destination extent from the point, and clip before the original kernel.
// Forward the unsigned color unchanged even though the Windows kernel does not read it.
u8 GxAlphaBltHalfAlphaVariableSource1ConstSource2(GxBuffer *source, GxBuffer *destination, u32 color,
    const ScRect &source_rect, const ScPoint &target, const ScRect *clip) {
  ScRect sr = source_rect,
         dr = {target.x, target.y, (i32)((u32)target.x + (u32)sr.right - (u32)sr.left),
             (i32)((u32)target.y + (u32)sr.bottom - (u32)sr.top)};
  if (!GxGenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint point{dr.left, dr.top};
  return GxAlphaBltHalfAlphaVariableSource1ConstSource2NoClip(source, destination, color, sr, point);
}

GxMode GxRequestedMode = {640, 480, 16, 0, 0, 0, 0};
u8 GxStartWindowed = 0, GxFlipping = 1, GxAdapterPreference[32] = {};
u32 GxPrimaryColorFrames = 1, GxPrimaryDepthFrames = 0, GxPrimaryReserveBytes = 0;

void GxGraphicsBase::PreInitSetDesiredGameResolution(const GxMode &mode) {
  GxRequestedMode = mode;
}

void GxGraphicsBase::PreInitSetHardware3DAccellerationUse(bool value) {
  GxFlipping = value;
}

void GxGraphicsBase::PreInitSetWindowedMode(bool value) {
  GxStartWindowed = value;
}

void GxGraphicsBase::PreInitSetDesiredVRAMForTextures(u32 bytes) {
  GxPrimaryReserveBytes = bytes;
}

void GxGraphicsBase::PreInitSetDesiredVideoDriverIdentifier(u8 *preference) {
  memcpy(GxAdapterPreference, preference, sizeof(GxAdapterPreference));
}

// Restore only the three default selection words; the existing trailing words remain unchanged.
void GxResetRequestedMode() {
  GxRequestedMode.width = 640;
  GxRequestedMode.height = 480;
  GxRequestedMode.depth = 16;
}

void GX_PIXEL_CDECL GxPack555(u16 *out, u8 red, u8 green, u8 blue) {
  u16 pixel = (u16)(((red & 248u) << 7) | ((green & 248u) << 2) | (blue >> 3));
  memcpy(out, &pixel, sizeof(pixel));
}

void GX_PIXEL_CDECL GxPack655(u16 *out, u8 red, u8 green, u8 blue) {
  u16 pixel = (u16)(((red & 124u) << 9) | ((green & 248u) << 3) | (blue >> 3));
  memcpy(out, &pixel, sizeof(pixel));
}

void GX_PIXEL_CDECL GxPack565(u16 *out, u8 red, u8 green, u8 blue) {
  u16 pixel = (u16)(((red & 248u) << 8) | ((green & 252u) << 3) | (blue >> 3));
  memcpy(out, &pixel, sizeof(pixel));
}

void GX_PIXEL_CDECL GxPack556(u16 *out, u8 red, u8 green, u8 blue) {
  u16 pixel = (u16)(((red & 248u) << 8) | ((green & 248u) << 3) | (blue >> 2));
  memcpy(out, &pixel, sizeof(pixel));
}

void GX_PIXEL_CDECL GxPack555Shifted(u16 *out, u8 red, u8 green, u8 blue) {
  u16 pixel = (u16)(((red & 248u) << 8) | ((green & 248u) << 3) | ((blue >> 2) & 254u));
  memcpy(out, &pixel, sizeof(pixel));
}

void GX_PIXEL_CDECL GxUnpack555(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255u * (pixel >> 10) / 31);
  *green = (u8)(255u * ((pixel >> 5) & 31) / 31);
  *blue = (u8)(255u * (pixel & 31) / 31);
}

void GX_PIXEL_CDECL GxUnpack655(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255u * (pixel >> 11) / 63);
  *green = (u8)(255u * ((pixel >> 5) & 31) / 31);
  *blue = (u8)(255u * (pixel & 31) / 31);
}

void GX_PIXEL_CDECL GxUnpack565(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255u * (pixel >> 11) / 31);
  *green = (u8)(255u * ((pixel >> 5) & 63) / 63);
  *blue = (u8)(255u * (pixel & 31) / 31);
}

void GX_PIXEL_CDECL GxUnpack556(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255u * (pixel >> 11) / 31);
  *green = (u8)(255u * ((pixel >> 6) & 31) / 31);
  *blue = (u8)(255u * (pixel & 63) / 63);
}

void GX_PIXEL_CDECL GxUnpack555Shifted(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(255u * (pixel >> 11) / 31);
  *green = (u8)(255u * ((pixel >> 6) & 31) / 31);
  *blue = (u8)(255u * ((pixel >> 1) & 31) / 31);
}


// The native output format replaces the display driver's selected pixel-format state.
u32 GxOutputFormat = 7;

