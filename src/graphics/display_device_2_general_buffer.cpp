#define WIN32_LEAN_AND_MEAN
#include "graphics/display_device.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <typeinfo>
#include "common/api.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/module.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"
#include "graphics/sc3_effects.h"
#include "graphics/sdl_platform.h"
#include "window/api.h"

// Replace the seed's connected color within the current logical bounds after the original virtual pixel and pitch lookups.
// Equal full DWORD colors and non-sixteen-bit formats return zero before requesting the physical plane.
u8 GxGeneralBuffer::FloodFill(i32 x, i32 y, u32 pixel) {
  GxFloodReplacement = pixel;
  GxFloodTarget = GetPixel(x, y);
  GxFloodPitch = GetColorSurfaceStride();
  GxFloodLeft = GxFloodTop = 0;
  GxFloodRight = area.right;
  GxFloodBottom = area.bottom;
  if (GxFloodReplacement == GxFloodTarget || format[1] != 16)
    return 0;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxFloodReplace(pixels + (i32)(2u * ((u32)x + ((u32)y * GxFloodPitch >> 1))), x, y);
  return 1;
}

// Replace the seed's connected color without logical clipping, using the original enclosing pixel boundary.
// Capture the color and pitch even when the equal-color or format gate rejects the operation.
u8 GxGeneralBuffer::FloodFillNoClip(i32 x, i32 y, u32 pixel) {
  GxFloodReplacement = pixel;
  GxFloodTarget = GetPixel(x, y);
  GxFloodPitch = GetColorSurfaceStride();
  if (GxFloodReplacement == GxFloodTarget || format[1] != 16)
    return 0;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxFloodReplaceUnclipped(pixels + (i32)(2u * ((u32)x + ((u32)y * GxFloodPitch >> 1))));
  return 1;
}

// Fill connected interior pixels until the supplied border or current logical extent is reached.
u8 GxGeneralBuffer::BoundaryFill(i32 x, i32 y, u32 pixel, u32 border) {
  GxFloodReplacement = pixel;
  GxFloodTarget = border;
  GxFloodPitch = GetColorSurfaceStride();
  GxFloodLeft = GxFloodTop = 0;
  GxFloodRight = area.right;
  GxFloodBottom = area.bottom;
  if (GxFloodReplacement == GxFloodTarget || format[1] != 16)
    return 0;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxFloodBorder(pixels + (i32)(2u * ((u32)x + ((u32)y * GxFloodPitch >> 1))), x, y);
  return 1;
}

// Fill to an enclosing border without logical clipping, preserving the raw-pitch vertical kernel.
// The seed address still uses WORD-rounded row displacement before that kernel begins.
u8 GxGeneralBuffer::BoundaryFillNoClip(i32 x, i32 y, u32 pixel, u32 border) {
  GxFloodReplacement = pixel;
  GxFloodTarget = border;
  GxFloodPitch = GetColorSurfaceStride();
  if (GxFloodReplacement == GxFloodTarget || format[1] != 16)
    return 0;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxFloodBorderUnclipped(pixels + (i32)(2u * ((u32)x + ((u32)y * GxFloodPitch >> 1))));
  return 1;
}

// Submit the four half-open outline edges through the clipped line operation, returning the last edge's byte.
u8 GxGeneralBuffer::DrawRectangleOutline(const ScRect &box_input, u32 pixel, u32 thickness) {
  const ScRect *box = &box_input;
  DrawLine(box->left, box->top, (i32)((u32)box->right - 1), box->top, static_cast<i32>((u16)pixel), thickness);
  DrawLine(box->left, (i32)((u32)box->bottom - thickness), (i32)((u32)box->right - 1),
      (i32)((u32)box->bottom - thickness), static_cast<i32>((u16)pixel), thickness);
  DrawLine((i32)((u32)box->right - thickness), box->top, (i32)((u32)box->right - thickness),
      (i32)((u32)box->bottom - 1), static_cast<i32>((u16)pixel), thickness);
  return DrawLine(box->left, box->top, box->left, (i32)((u32)box->bottom - 1), static_cast<i32>((u16)pixel), thickness);
}

// Read only the BYTE outputs written by buffer404 and pass them to the real RGB bevel operation.
u8 GxGeneralBuffer::DrawRaisedRect(const ScRect &box_input, u32 pixel, bool raised, bool filled) {
  const ScRect *box = &box_input;
  u8 red, green, blue;
  Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  return DrawRaisedRect(*(box), red, green, blue, raised, filled);
}

// Adjust blue, green and red by the same wrapped signed delta, saturate each, then invoke the real RGB virtual.
// Return the complete zero-extended low WORD even when the virtual provider returns additional high bits.
u32 GxAdjustRgb(GxBuffer *buffer, u8 red, u8 green, u8 blue, i32 delta) {
  if (delta) {
    blue = GxSoftwareShade(blue, delta);
    green = GxSoftwareShade(green, delta);
    red = GxSoftwareShade(red, delta);
  }
  return buffer->ConvertRGBValueToNative16Bit(red, green, blue);
}

u8 GxGeneralBuffer::DrawRaisedRect(const ScRect &box_input, u8 red, u8 green, u8 blue, bool raised, bool filled) {
  const ScRect *box = &box_input;
  i32 shift = raised ? 56 : -56;
  u16 center = ConvertRGBValueToNative16Bit(red, green, blue);
  u16 light = (u16)GxAdjustRgb(this, red, green, blue, shift);
  u16 dark = (u16)GxAdjustRgb(this, red, green, blue, -shift);
  bool failed = false;
  if (filled && Lock(GxLockFlags::Fill)) {
    failed = !Fill(center, *box, nullptr);
    Unlock(GxLockFlags::Fill);
  }
  if (Lock(GxLockFlags::Line)) {
    failed |= !DrawLine(box->left, box->top, (i32)((u32)box->right - 1), box->top, static_cast<i32>(light), 1);
    failed |= !DrawLine(box->left, box->top, box->left, (i32)((u32)box->bottom - 1), static_cast<i32>(light), 1);
    failed |= !DrawLine((i32)((u32)box->right - 1), box->top, (i32)((u32)box->right - 1), (i32)((u32)box->bottom - 1),
        static_cast<i32>(dark), 1);
    failed |= !DrawLine(box->left, (i32)((u32)box->bottom - 1), (i32)((u32)box->right - 1), (i32)((u32)box->bottom - 1),
        static_cast<i32>(dark), 1);
    Unlock(GxLockFlags::Line);
  }
  return !failed;
}

// Borrow the current palette without changing its reference count.
GxPaletteView *GxGeneralBuffer::GetPalette() const {
  return palette;
}

// Clip through this buffer before selecting a physical same-depth copy or the source conversion operation.
u8 GxGeneralBuffer::Blt(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  ScRect sr = *source_box, dr = *destination;
  if (!GenericBufferClip(this, &sr, &dr, clip))
    return 0;
  return GxGeneralBuffer::BltNoClip(source, sr, dr);
}

// Store the supplied color before dispatching the ordinary clipped receive operation with a null clip.
// Keep that color state after the transfer and forward the callee's raw byte result.
u8 GxGeneralBuffer::Blt(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const GxBltFlags &value_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  const GxBltFlags *value = &value_input;
  SetBltFlags(*(value));
  return Blt(source, *(source_box), *(destination), nullptr);
}

// Store the supplied color before directly dispatching the ordinary receive core without clipping.
u8 GxGeneralBuffer::BltNoClip(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const GxBltFlags &value_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  const GxBltFlags *value = &value_input;
  SetBltFlags(*(value));
  return BltNoClip(source, *(source_box), *(destination));
}

// Scale-clip local rectangles through this receiver before comparing its live depth with the source's returned format.
// Equal depths use the physical drivers directly; unequal depths use the separate scaled conversion operation.
u8 GxGeneralBuffer::BltStretch(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  ScRect sr = *source_box, dr = *destination;
  if (!GenericBufferClipStretch(this, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  GxColorType input = source->GetColorType();
  if (format[1] == static_cast<i32>(input.depth))
    return backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
  return PaletteBlitStretchNoClip(source, sr, dr, nullptr);
}

// Receive equal-depth pixels through the source's retained secondary identity and its borrowed backend getter.
// Release the identity after the physical call, preserving its byte across Release. Unequal depths convert directly.
u8 GxGeneralBuffer::BltStretchNoClip(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  GxColorType input = source->GetColorType();
  if (format[1] != static_cast<i32>(input.depth))
    return PaletteBlitStretchNoClip(source, *source_box, *destination, nullptr);
  void *view = nullptr;
  if (!source->QueryInterface(IID_GxBufferSecondary, &view))
    return 0;
  GxBufferSecondary *secondary = static_cast<GxBufferSecondary *>(view);
  GxDrawSurface *physical = secondary->GetBackend();
  u8 result = backend->Blit(physical, source_box, destination);
  secondary->Release();
  return result;
}

// Keep the supplied color state while dispatching scaled clipping with the original null clip argument.
u8 GxGeneralBuffer::BltStretch(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const GxBltFlags &value_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  const GxBltFlags *value = &value_input;
  SetBltFlags(*(value));
  return BltStretch(source, *(source_box), *(destination), nullptr);
}

// Keep the supplied color state while dispatching the separate scaled receive core without clipping.
u8 GxGeneralBuffer::BltStretchNoClip(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const GxBltFlags &value_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  const GxBltFlags *value = &value_input;
  SetBltFlags(*(value));
  return BltStretchNoClip(source, *(source_box), *(destination));
}

// Clip the transfer through this source's clip method with the destination as the explicit geometry receiver.
u8 GxGeneralBuffer::BltSrc(
    GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination_box = &destination_box_input;
  ScRect sr = *source_box, dr = *destination_box;
  if (!GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  return GxGeneralBuffer::BltNoClipSrc(destination, sr, dr);
}

// Dispatch conversion for the buffer classes registered in the statically linked production executable.
static u8 GxConvertDestination(
    GxBuffer *destination, GxBuffer *source, const ScRect *source_rect, const ScRect *target, bool scaled) {
  if (GxGeneralBuffer *buffer = dynamic_cast<GxGeneralBuffer *>(destination))
    return scaled ? buffer->PaletteBlitStretchNoClip(source, *source_rect, *target, nullptr)
                  : buffer->PaletteBlitNoClip(source, *source_rect, *target, nullptr);
  assert(dynamic_cast<GxImage *>(destination) && typeid(*destination) == typeid(GxImage));
  assert(!scaled);
  return 0;
}

// Resolve the destination's returned format before reading this source's live depth and physical driver.
// Equal depths submit the current surfaces; unequal depths use the destination's conversion operation.
u8 GxGeneralBuffer::BltNoClipSrc(
    GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination_box = &destination_box_input;
  GxColorType output = destination->GetColorType();
  if (format[1] == static_cast<i32>(output.depth))
    return static_cast<GxGeneralBuffer *>(destination)->backend->Blit(backend, source_box, destination_box);
  return GxConvertDestination(destination, this, source_box, destination_box, false);
}

// Scale-clip local rectangles before selecting the destination's physical surface or its separate conversion entry.
u8 GxGeneralBuffer::BltStretchSrc(
    GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination_box = &destination_box_input;
  ScRect sr = *source_box, dr = *destination_box;
  if (!GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  return GxGeneralBuffer::BltStretchNoClipSrc(destination, sr, &dr);
}

// Read this source's live depth only after the destination's format callback returns its selected pair.
// Preserve the physical driver's raw byte for equal depths and the scaled conversion result for unequal depths.
u8 GxGeneralBuffer::BltStretchNoClipSrc(
    GxBuffer *destination, const ScRect &source_box_input, ScRect *destination_box) {
  const ScRect *source_box = &source_box_input;
  GxColorType output = destination->GetColorType();
  if (format[1] == static_cast<i32>(output.depth))
    return static_cast<GxGeneralBuffer *>(destination)->backend->Blit(backend, source_box, destination_box);
  return GxConvertDestination(destination, this, source_box, destination_box, true);
}

u8 GxGeneralBuffer::PaletteBlitStretchNoClip(GxBuffer *, const ScRect &, const ScRect &, void *) {
  return 0;
}

// Convert indexed or BGR DWORD rows into WORD output. Failed locks retain the original success return and lock effects.
u8 GxGeneralBuffer::PaletteBlitNoClip(
    GxBuffer *source, const ScRect &source_rect, const ScRect &destination_rect, void *palette_data) {
  const ScRect *sr = &source_rect, *dr = &destination_rect;
  const u8 *colors = static_cast<const u8 *>(palette_data);
  u8 result = 1;
  if (source->Lock(GxLockFlags::Bits) && Lock(GxLockFlags::Bits)) {
    if (source->GetColorType().depth == 8 && format[1] == 16) {
      if (!colors) {
        GxPaletteView *source_palette = source->GetPalette();
        if (source_palette)
          colors = static_cast<const u8 *>(source_palette->GetPaletteArray());
      }
      if (colors) {
        u8 transparent = (u8)source->GetBltFlags()->pixel;
        i32 width = sr->right - sr->left;
        u32 source_pitch = source->GetColorSurfaceStride(), target_pitch = GetColorSurfaceStride() >> 1;
        u8 *input = static_cast<u8 *>(source->GetColorSurfaceBits());
        input += (iptr)sr->top * source_pitch + sr->left;
        u8 *output = static_cast<u8 *>(GetColorSurfaceBits());
        output += 2 * ((iptr)dr->top * target_pitch + dr->left);
        i32 height = sr->bottom - sr->top;
        for (i32 y = 0; source_pitch && y < height; ++y) {
          for (i32 x = 0; x < width; ++x) {
            u8 index = input[(usize)y * source_pitch + x];
            if (index != transparent)
              memcpy(output + 2 * ((usize)y * target_pitch + x), colors + 2 * index, 2);
          }
        }
      } else
        result = 0;
    } else if (source->GetColorType().depth == 32 && format[1] == 16) {
      i32 width = sr->right - sr->left;
      u32 source_pitch = source->GetColorSurfaceStride() >> 2, target_pitch = GetColorSurfaceStride() >> 1;
      u8 *input = static_cast<u8 *>(source->GetColorSurfaceBits());
      input += 4 * ((iptr)sr->top * source_pitch + sr->left);
      u8 *output = static_cast<u8 *>(GetColorSurfaceBits());
      output += 2 * ((iptr)dr->top * target_pitch + dr->left);
      i32 height = sr->bottom - sr->top, target = static_cast<i32>(GetColorType().color);
      if (target != 7)
        target = static_cast<i32>(GetColorType().color) == 5 ? 5 : 0;
      if (target) {
        for (i32 y = 0; source_pitch && y < height; ++y) {
          for (i32 x = 0; x < width; ++x) {
            u32 pixel;
            memcpy(&pixel, input + 4 * ((usize)y * source_pitch + x), 4);
            u16 word = target == 7 ? (u16)(((pixel >> 8) & 0xf800) | ((pixel >> 5) & 0x7e0) | ((pixel >> 3) & 31))
                                   : (u16)(((pixel >> 9) & 0x7c00) | ((pixel >> 6) & 0x3e0) | ((pixel >> 3) & 31));
            memcpy(output + 2 * ((usize)y * target_pitch + x), &word, 2);
          }
        }
      }
    } else
      result = 0;
    Unlock(GxLockFlags::Bits);
    source->Unlock(GxLockFlags::Bits);
  }
  return result;
}

// Publish the owned driver surface before virtual setup; failed initialization destroys the reloaded surface.
bool GxGeneralBuffer::Setup() {
  backend = GxCreateSurface();
  if (InitBackends())
    return true;
  delete backend;
  backend = nullptr;
  return false;
}

// Initialize the primary before applying color, cache policy and indexed palette, then allocate and attach any depth plane.
bool GxGeneralBuffer::InitBackends() {
  if (!backend->Init((u32)area.right, (u32)area.bottom, format, mode, mode_flag))
    return false;
  backend->SetColor(&color);
  if (backend->allocation_policy) {
    if (restore_type != GxRestoreType::None) {
      GxRestoreType requested = restore_type;
      restore_type = GxRestoreType::None;
      SetSurfaceRestoreType(requested);
    }
    backend->owner = this;
  } else {
    SetSurfaceRestoreType(GxRestoreType::None);
    backend->owner = nullptr;
  }
  i32 code = static_cast<i32>(GetColorType().color);
  if ((code == 2 || code == 3) && palette)
    backend->SetPalette(palette);
  if (static_cast<i32>(GetZDepth().depth) > 0) {
    return false;
  }
  return true;
}

// Close and delete the secondary surface before the primary, reloading each live member after Close.
bool GxGeneralBuffer::CloseBackends() {
  if (secondary_backend) {
    secondary_backend->Close();
    delete secondary_backend;
    secondary_backend = nullptr;
  }
  if (backend) {
    backend->Close();
    delete backend;
    backend = nullptr;
  }
  return true;
}

GxDepthType GxGeneralBuffer::GetZDepth() const {
  return GxDepthType(static_cast<GxDepthType::Depth>(secondary_format));
}

// Forward the stored blit flags to each reached live backend, preserving the original void contract.
void GxGeneralBuffer::SetBltFlags(const GxBltFlags &value_input) {
  const GxBltFlags *value = &value_input;
  GxDrawSurface *primary = backend;
  color = *value;
  if (primary)
    primary->SetColor(&color);
  if (secondary_backend)
    secondary_backend->SetColor(&color);
}

// Opcode selection uses only the incoming flags, while the stored color flags accumulate their full DWORD mask.
void GxGeneralBuffer::EnableZBufferBlit(GxBltFlags::Options flags_value) {
  u32 flags = static_cast<u32>(flags_value);
  color.flags |= flags;
  if (!GxDisableColorEffects) {
    if (backend) {
      backend->depth_operation = flags & 0x80 ? 4 : flags & 0x40 ? 2 : 8;
    }
    if (secondary_backend) {
      secondary_backend->depth_operation = flags & 0x80 ? 4 : flags & 0x40 ? 2 : 8;
    }
  }
}

// Preserve unrelated flags, raster operations and destination constants rather than reapplying the whole color record.
void GxGeneralBuffer::DisableZBufferBlit() {
  color.flags &= ~0x3e0u;
  if (!GxDisableColorEffects) {
    if (backend) {
      backend->depth_operation = 0;
    }
    if (secondary_backend) {
      secondary_backend->depth_operation = 0;
    }
  }
}

// Create the original empty temporary file and publish its generated path through the owned string.
// Either Win32 failure leaves the output string unchanged; the caller intentionally ignores this result.
static bool GxCreateCachePath(ScString *out) {
  char *directory = SDL_GetPrefPath("Maxis", "SimCity 3000 Unlimited");
  if (!directory)
    return false;
  ScString filename;
  FILE *file = nullptr;
  for (u32 attempt = 0; attempt < 16 && !file; ++attempt) {
    filename.Sprintf("%sGZB_%llu_%u.tmp", directory, (unsigned long long)SDL_GetTicksNS(), attempt);
#if defined(_MSC_VER)
    fopen_s(&file, filename.Data(), "wbx");
#else
    file = fopen(filename.Data(), "wbx");
#endif
  }
  SDL_free(directory);
  if (!file)
    return false;
  fclose(file);
  out->FromChar(filename.Data());
  return true;
}

void GxGeneralBuffer::SetSurfaceRestoreFunction(GxRestoreCallback callback, void *first, void *second) {
  if (restore_type != GxRestoreType::Callback)
    SetSurfaceRestoreType(GxRestoreType::Callback);
  restore_callback = callback;
  cache_data = first;
  restore_argument = second;
}

// Select the original restoration policy, retaining snapshot and temporary-path ownership across unchanged requests.
// Actual transitions clear the callback and both payload fields before querying initialization.
void GxGeneralBuffer::SetSurfaceRestoreType(GxRestoreType requested) {
  if (requested == GxRestoreType::Default)
    requested = GxRestoreType::None;
  if (requested != GxRestoreType::None && backend && backend->ready && !backend->allocation_policy)
    requested = GxRestoreType::None;
  if (requested == restore_type)
    return;
  // Source mode2 frees the allocation directly, without reading snapshot fields or running any virtual destructor.
  if (restore_type == GxRestoreType::AutoRam)
    ::operator delete(cache_data);
  else if (restore_type == GxRestoreType::AutoHd)
    delete static_cast<ScString *>(cache_data);
  restore_callback = nullptr;
  cache_data = nullptr;
  restore_argument = nullptr;
  if (IsReady()) {
    if (requested == GxRestoreType::AutoRam) {
      GxBuffer *snapshot;
      Duplicate(&snapshot, static_cast<GxVramRequest>(0));
      cache_data = snapshot;
    } else if (requested == GxRestoreType::AutoHd) {
      ScString *path = new ScString;
      GxCreateCachePath(path);
      // The installed filename-transfer entry10001EBD is constant false and has no file-content effects.
      cache_data = path;
    }
  }
  restore_type = requested;
  if (backend)
    backend->owner = this;
}

// Capture into or restore from the existing snapshot through the original lock16 and direct blit contracts.
// The transfer result is retained, and unlock is performed only after a successful destination lock.
u8 GxGeneralBuffer::CopySnapshot(GxBuffer *snapshot, u8 capture) {
  u8 result = 0;
  if (capture) {
    if (snapshot && snapshot->Lock(GxLockFlags::Blt)) {
      result = snapshot->BltNoClip(this, area, area);
      snapshot->Unlock(GxLockFlags::Blt);
    }
  } else if (snapshot && Lock(GxLockFlags::Blt)) {
    result = BltNoClip(snapshot, area, area);
    Unlock(GxLockFlags::Blt);
  }
  return result;
}

// Refresh a buffer snapshot; the installed filename-cache transfer is constant false and other modes are no-ops.
u8 GxGeneralBuffer::RefreshSurfaceRestoreImage() {
  if (restore_type == GxRestoreType::AutoRam)
    return CopySnapshot(static_cast<GxBuffer *>(cache_data), 1);
  return restore_type == GxRestoreType::AutoHd ? 0 : 1;
}

// Restore the selected cache representation or call the original external callback with both live opaque payloads.
u8 GxGeneralBuffer::Restore() {
  if (restore_type == GxRestoreType::AutoRam)
    return CopySnapshot(static_cast<GxBuffer *>(cache_data), 0);
  if (restore_type == GxRestoreType::AutoHd)
    return 0;
  if (restore_type == GxRestoreType::Callback && restore_callback)
    return restore_callback(cache_data, restore_argument);
  return 1;
}

// Convert a supported unequal WORD in place; the two high-bit-only branches do not read its low byte.
u8 GxGeneralBuffer::Convert16BitValueFromFormatToFormat(
    u16 *pixel, GxColorType::Color source_value, GxColorType::Color target_value) {
  using Color = GxColorType::Color;
  Color source = source_value, target = target_value;
  if (target == Color::Native)
    target = static_cast<Color>(format[0]);
  if (source == target)
    return 1;
  if (!GxConvertSupported(source, target))
    return 0;
  u8 *bytes = (u8 *)pixel;
  if ((source == Color::RGB555 && target == Color::ARGB1555) || (source == Color::ARGB1555 && target == Color::RGB555))
    bytes[1] |= 128;
  else {
    u16 value;
    memcpy(&value, bytes, 2);
    value = GxConvertValue(value, source, target);
    memcpy(bytes, &value, 2);
  }
  return 1;
}

// Move a source rectangle to an absolute position while preserving its DWORD width and height.
static void GxMoveRect(ScRect *rectangle, i32 x, i32 y) {
  u32 dx = (u32)x - (u32)rectangle->left, dy = (u32)y - (u32)rectangle->top;
  rectangle->right = (i32)((u32)rectangle->right + dx);
  rectangle->bottom = (i32)((u32)rectangle->bottom + dy);
  rectangle->left = x;
  rectangle->top = y;
}

// Draw the four source corners directly and tile the edges plus optional center in original row order.
// All submission results are ignored; only the flag's low byte controls the center and the method returns one.
u8 GxGeneralBuffer::BltEdge(
    GxBuffer *source, const ScRect &slice_input, const ScRect &destination_input, u8 flags_value) {
  const ScRect *slice = &slice_input;
  const ScRect *destination = &destination_input;
  u32 flags = static_cast<u32>(flags_value);
  ScRect sr = *slice, dr = *slice;
  u32 width = (u32)slice->right - (u32)slice->left, height = (u32)slice->bottom - (u32)slice->top;
  GxMoveRect(&dr, destination->left, destination->top);
  backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = (i32)(width + (u32)destination->left);
  dr.right = (i32)((u32)destination->right - width);
  if (dr.left < dr.right)
    BltTiled(source, sr, dr, 0, 0, nullptr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = dr.right;
  dr.right = destination->right;
  backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
  GxMoveRect(&sr, slice->left, (i32)(height + (u32)slice->top));
  dr.left = destination->left;
  dr.right = (i32)(width + (u32)dr.left);
  dr.top = dr.bottom;
  dr.bottom = (i32)((u32)destination->bottom - height);
  if (dr.top < dr.bottom)
    BltTiled(source, sr, dr, 0, 0, nullptr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = dr.right;
  dr.right = (i32)((u32)destination->right - width);
  if ((u8)flags && dr.left < dr.right && dr.top < dr.bottom)
    BltTiled(source, sr, dr, 0, 0, nullptr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = dr.right;
  dr.right = destination->right;
  if (dr.top < dr.bottom)
    BltTiled(source, sr, dr, 0, 0, nullptr);
  GxMoveRect(&sr, slice->left, sr.bottom);
  dr.left = destination->left;
  dr.right = (i32)(width + (u32)dr.left);
  dr.top = dr.bottom;
  dr.bottom = (i32)((u32)dr.bottom + height);
  backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = dr.right;
  dr.right = (i32)((u32)destination->right - width);
  if (dr.left < dr.right)
    BltTiled(source, sr, dr, 0, 0, nullptr);
  sr.left = (i32)((u32)sr.left + width);
  sr.right = (i32)((u32)sr.right + width);
  dr.left = dr.right;
  dr.right = destination->right;
  backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
  return 1;
}

// Resolve the source's returned format before reading this receiver's live depth and physical driver.
// Submit equal-depth buffers directly; unequal depths use this receiver's virtual conversion entry.
u8 GxGeneralBuffer::BltNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  GxColorType input = source->GetColorType();
  if (format[1] == static_cast<i32>(input.depth))
    return backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, source_box, destination);
  return PaletteBlitNoClip(source, *source_box, *destination, nullptr);
}

bool GxGeneralBuffer::IsZBuffered() const {
  return secondary_format > 0;
}

// Derive the shared copy rectangle through live source-area queries, then lock, blit and unlock.
// The retail void declaration confirms that both submission results and final unlock residue are discarded.
void GxGeneralBuffer::CopyBuffer(GxBuffer *source) {
  ScRect rectangle = {};
  const ScRect *bounds = source->GetBufferArea();
  i32 width = (i32)((u32)bounds->right - (u32)bounds->left);
  i32 own_width = (i32)((u32)area.right - (u32)area.left);
  if (width >= own_width)
    rectangle.right = own_width;
  else {
    bounds = source->GetBufferArea();
    rectangle.right = (i32)((u32)bounds->right - (u32)bounds->left);
  }
  bounds = source->GetBufferArea();
  i32 height = (i32)((u32)bounds->bottom - (u32)bounds->top);
  i32 own_height = (i32)((u32)area.bottom - (u32)area.top);
  if (height >= own_height)
    rectangle.bottom = own_height;
  else {
    bounds = source->GetBufferArea();
    rectangle.bottom = (i32)((u32)bounds->bottom - (u32)bounds->top);
  }
  Lock(GxLockFlags::Blt);
  Blt(source, rectangle, rectangle, nullptr);
  Unlock(GxLockFlags::Blt);
}

void GxGeneralBuffer::CopyZBuffer(GxBuffer *source) {
  if (IsZBuffered() && source->IsZBuffered())
    ZBlt(source, 256);
}

// Copy live secondary-driver rows after target then source locks; source driver identity is captured before either callback.
// A failed source lock retains the target lock, while success unlocks the live target before the captured source.
u8 GxGeneralBuffer::ZBlt(GxBuffer *source, u32) {
  GxDrawDepthSurface *input = static_cast<GxGeneralBuffer *>(source)->secondary_backend;
  if (!secondary_backend->Lock() || !input->Lock())
    return 0;
  GxDrawDepthSurface *output = secondary_backend;
  u32 source_pitch = input->pitch, target_pitch = output->pitch;
  u32 width = output->surface_width * (output->bits >> 3);
  u8 *source_pixels = input->data, *target_pixels = output->data;
  u32 height = output->surface_height;
  for (u32 y = 0; y < height; ++y)
    memcpy(target_pixels + (usize)y * target_pitch, source_pixels + (usize)y * source_pitch, width);
  secondary_backend->Unlock();
  input->Unlock();
  return 1;
}

// Publish an unretained same-format buffer before initialization, then copy ordinary pixels and any source secondary
// plane.
u8 GxGeneralBuffer::Duplicate(GxBuffer **out, GxVramRequest requested_mode_value) {
  u32 requested_mode = static_cast<u32>(requested_mode_value);
  u32 selected_mode = requested_mode == UINT32_MAX ? mode : requested_mode;
  GxGeneralBuffer *copy = new GxGeneralBuffer;
  *out = copy;
  GxColorType requested(static_cast<GxColorType::Color>(format[0]));
  requested.depth = static_cast<u32>(format[1]);
  if (copy->Init((u32)area.right, (u32)area.bottom, requested, palette,
          GxDepthType(static_cast<GxDepthType::Depth>(secondary_format)), static_cast<GxVramRequest>(selected_mode),
          mode_flag)) {
    (*out)->CopyBuffer(this);
    if (IsZBuffered())
      (*out)->CopyZBuffer(this);
    return true;
  }
  delete copy;
  *out = nullptr;
  return false;
}

// Rotate BYTE/WORD pixels with source-only locking and optional source color key, leaving the clip argument unread.
// Preserve WORD-only angle reduction and the default WORD branch's byte-scaled destination x coordinate.
u8 GxGeneralBuffer::RotateBltSimple(
    GxBuffer *source, const ScRect &source_rect, const ScPoint &destination, u32 angle, const ScRect *) {
  u8 result = source->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  i32 depth = format[1];
  if (depth == 16 || depth == 8) {
    u32 unit = depth == 16 ? 2u : 1u;
    u32 width = (u32)source_rect.right - (u32)source_rect.left;
    u32 height = (u32)source_rect.bottom - (u32)source_rect.top;
    u32 source_pitch = source->GetColorSurfaceStride(), target_pitch = GetColorSurfaceStride(),
        row_bytes = unit * width;
    uptr input = (uptr)source->GetColorSurfaceBits();
    input += (iptr)(i32)(source_pitch * (u32)source_rect.top + unit * (u32)source_rect.left);
    uptr row_end = input + (iptr)(i32)row_bytes;
    uptr input_end = input + (iptr)(i32)(height * source_pitch);
    if (depth == 16 && angle >= 360)
      angle %= 360;
    uptr output = (uptr)GetColorSurfaceBits();
    u32 x = (u32)destination.x, y = (u32)destination.y, offset, pixel_step, row_step;
    if (angle == 90) {
      offset = target_pitch * (y + width - 1) + unit * x;
      pixel_step = 0u - target_pitch;
      row_step = width * target_pitch + unit;
    } else if (angle == 180) {
      offset = target_pitch * (y + height - 1) + unit * (width + x) - unit;
      pixel_step = 0u - unit;
      row_step = row_bytes - target_pitch;
    } else if (angle == 270) {
      offset = target_pitch * y + unit * (height + x) - unit;
      pixel_step = target_pitch;
      row_step = 0u - unit - width * target_pitch;
    } else {
      offset = x + target_pitch * y;
      pixel_step = unit;
      row_step = target_pitch - row_bytes;
    }
    output += (iptr)(i32)offset;
    bool keyed = source->GetTransparency();
    u32 key = keyed ? source->GetBltFlags()->pixel & (unit == 2 ? 0xffffu : 0xffu) : 0;
    while (input < input_end) {
      while (input < row_end) {
        u16 pixel = 0;
        memcpy(&pixel, (const void *)input, unit);
        if (!keyed || pixel != key)
          memcpy((void *)output, &pixel, unit);
        input += unit;
        output += (iptr)(i32)pixel_step;
      }
      input += (iptr)(i32)(source_pitch - row_bytes);
      output += (iptr)(i32)row_step;
      row_end += (iptr)(i32)source_pitch;
    }
  }
  source->Unlock(GxLockFlags::Bits);
  return 1;
}

// Native representations of the original transform's two floating coordinates and raw signed16.16 coordinates.
struct GxRotationPoint {
  f32 x, y;
};

struct GxRotationFixed {
  u32 x, y;
};

static void GxRotationIntersection(ScRect *out, const ScRect *first, const ScRect *second) {
  if (first->right <= second->left || first->left >= second->right || first->bottom <= second->top ||
      first->top >= second->bottom) {
    out->bottom = 0;
    out->top = 0;
    out->right = 0;
    out->left = 0;
    return;
  }
  out->left = first->left > second->left ? first->left : second->left;
  out->top = first->top > second->top ? first->top : second->top;
  out->right = first->right < second->right ? first->right : second->right;
  out->bottom = first->bottom < second->bottom ? first->bottom : second->bottom;
}

static u32 GxRotationInteger(f64 value) {
  assert(value >= -0x1p63 && value < 0x1p63);
  return (u32)(i64)value;
}

// Rotate one floating point about the captured center in radians, retaining the original intermediate float stores.
static void GxRotatePoint(GxRotationPoint *point, const GxRotationPoint &center, f32 angle) {
  f32 x = (f32)((f64)point->x - center.x), y = (f32)((f64)point->y - center.y);
  f32 cosine = (f32)cos((f64)angle);
  f64 sine = sin((f64)angle);
  point->x = (f32)((f64)cosine * x + center.x - sine * y);
  point->y = (f32)((f64)cosine * y + sine * x + center.y);
}

static u32 GxRotationMultiply(u32 first, u32 second) {
  return (u32)(((i64)(i32)first * (i32)second) >> 16);
}

static void GxRotateFixed(GxRotationFixed *point, const GxRotationPoint &center, f32 angle) {
  u32 cx = GxRotationInteger((f64)center.x * 65536.0), cy = GxRotationInteger((f64)center.y * 65536.0);
  u32 x = point->x - cx, y = point->y - cy;
  f32 cosine = (f32)cos((f64)angle);
  f64 sine = sin((f64)angle);
  u32 fixed_sine = GxRotationInteger(sine * 65536.0), fixed_cosine = GxRotationInteger((f64)cosine * 65536.0);
  point->x = cx - GxRotationMultiply(y, fixed_sine) + GxRotationMultiply(x, fixed_cosine);
  point->y = cy + GxRotationMultiply(y, fixed_cosine) + GxRotationMultiply(x, fixed_sine);
}

// Scan the four rotated edges at integer rows, preserving their half-open crossings and nonvertical plus-one bias.
static void GxRotationEdges(const GxRotationPoint *points, i32 rows, i32 width, i32 *minimum, i32 *maximum) {
  for (i32 y = 0; y < rows; ++y) {
    minimum[y] = INT32_MAX;
    maximum[y] = -INT32_MAX;
    for (u32 index = 0; index < 4; ++index) {
      const GxRotationPoint &first = points[index], &second = points[(index + 1) & 3];
      if ((first.y <= (f64)y && second.y > (f64)y) || (second.y <= (f64)y && first.y > (f64)y)) {
        f64 crossing = first.x;
        if (first.x != second.x)
          crossing = ((f64)y - second.y) / (((f64)first.y - second.y) / ((f64)first.x - second.x)) + second.x + 1.0;
        i32 x = (i32)GxRotationInteger(crossing);
        if (x < minimum[y])
          minimum[y] = x;
        if (x > maximum[y])
          maximum[y] = x;
      }
    }
    if (minimum[y] < 0)
      minimum[y] = 0;
    if (maximum[y] >= width)
      maximum[y] = width;
  }
}

// Rasterize a radian-rotated source rectangle through the clipped destination grid and inverse16.16 WORD sampling.
u8 GxGeneralBuffer::RotateBlt(
    GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input, f32 angle, const ScRect *clip) {
  const ScRect *source_rect = &source_rect_input;
  const ScRect *destination = &destination_input;
  GxRotationPoint center = {(f32)((f64)(i32)((u32)source_rect->left + (u32)source_rect->right) * 0.5),
      (f32)((f64)(i32)((u32)source_rect->top + (u32)source_rect->bottom) * 0.5)};
  f32 half_width = (f32)((f64)(i32)((u32)destination->right - (u32)destination->left) * 0.5);
  f32 half_height = (f32)((f64)(i32)((u32)destination->bottom - (u32)destination->top) * 0.5);
  ScRect rectangle;
  GxRotationIntersection(&rectangle, destination, &area);
  if (clip)
    GxRotationIntersection(&rectangle, &rectangle, clip);
  i32 rows = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
  i32 minimum_stack[512], maximum_stack[512], *minimum = minimum_stack, *maximum = maximum_stack;
  bool heap = (u32)rows > 512;
  if (heap) {
    u32 storage = ((u32)rows * 4u) >> 2;
    assert(rows <= 0 || storage >= (u32)rows);
    minimum = new i32[storage];
    maximum = new i32[storage];
  }
  GxRotationPoint corners[4] = {{(f32)source_rect->left, (f32)source_rect->top},
      {(f32)source_rect->left, (f32)source_rect->bottom}, {(f32)source_rect->right, (f32)source_rect->bottom},
      {(f32)source_rect->right, (f32)source_rect->top}};
  f32 negative_angle = -angle;
  for (u32 index = 0; index < 4; ++index)
    GxRotatePoint(corners + index, center, negative_angle);
  f64 dx = (f64)half_width - center.x, dy = (f64)half_height - center.y;
  i32 offset_x = (i32)((u32)destination->left - (u32)rectangle.left);
  i32 offset_y = (i32)((u32)destination->top - (u32)rectangle.top);
  for (u32 index = 0; index < 4; ++index) {
    corners[index].x = (f32)((index ? (f64)(f32)offset_x : (f64)offset_x) + dx + corners[index].x);
    corners[index].y = (f32)((index ? (f64)(f32)dy : dy) + corners[index].y);
    corners[index].y = (f32)((f64)(f32)offset_y + corners[index].y);
  }
  GxRotationEdges(corners, rows, (i32)((u32)rectangle.right - (u32)rectangle.left), minimum, maximum);
  GxRotationPoint zero = {0, 0};
  GxRotationFixed step = {65536, 0};
  GxRotateFixed(&step, zero, angle);
  if (format[1] == 16 && source->Lock(GxLockFlags::Bits)) {
    bool keyed = source->GetTransparency();
    u32 target_pitch = GetColorSurfaceStride() >> 1, source_pitch = source->GetColorSurfaceStride() >> 1;
    u8 *target_data = static_cast<u8 *>(GetColorSurfaceBits()),
       *source_data = static_cast<u8 *>(source->GetColorSurfaceBits());
    u16 key = (u16)source->GetBltFlags()->pixel;
    for (i32 y = 0; y < rows; ++y) {
      i32 start = minimum[y], finish = maximum[y];
      i32 relative_x = (i32)((u32)start + (u32)rectangle.left - (u32)destination->left);
      i32 relative_y = (i32)((u32)y + (u32)rectangle.top - (u32)destination->top);
      GxRotationFixed position = {GxRotationInteger(((f64)relative_x - half_width + center.x) * 65536.0),
          GxRotationInteger(((f64)relative_y - half_height + center.y) * 65536.0)};
      GxRotateFixed(&position, center, angle);
      position.x += 98;
      position.y += 98;
      u32 target_row = 2u * ((u32)rectangle.top + (u32)y) * target_pitch;
      for (i32 x = start; x < finish; ++x) {
        u32 source_offset = 2u * ((u32)((i32)position.x >> 16) + source_pitch * (u32)((i32)position.y >> 16));
        u16 value;
        memcpy(&value, GxPixelAddress(source_data, source_offset), sizeof(value));
        if (!keyed || value != key)
          memcpy(GxPixelAddress(target_data, target_row + 2u * ((u32)rectangle.left + (u32)x)), &value, sizeof(value));
        position.x += step.x;
        position.y += step.y;
      }
    }
    source->Unlock(GxLockFlags::Bits);
  }
  if (heap) {
    delete[] minimum;
    delete[] maximum;
  }
  return 1;
}

// Rasterize a scaled and radian-rotated source rectangle through the clipped destination grid and inverse16.16 WORD sampling.
u8 GxGeneralBuffer::RotateBltStretch(GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input,
    f32 angle, f32 scale, const ScRect *clip) {
  const ScRect *source_rect = &source_rect_input;
  const ScRect *destination = &destination_input;
  f32 inverse = (f32)(1.0 / scale);
  GxRotationPoint center = {(f32)((f64)(i32)((u32)source_rect->left + (u32)source_rect->right) * 0.5),
      (f32)((f64)(i32)((u32)source_rect->top + (u32)source_rect->bottom) * 0.5)};
  f32 half_width = (f32)((f64)(i32)((u32)destination->right - (u32)destination->left) * 0.5);
  f32 half_height = (f32)((f64)(i32)((u32)destination->bottom - (u32)destination->top) * 0.5);
  ScRect rectangle;
  GxRotationIntersection(&rectangle, destination, &area);
  if (clip)
    GxRotationIntersection(&rectangle, &rectangle, clip);
  i32 rows = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
  i32 minimum_stack[512], maximum_stack[512], *minimum = minimum_stack, *maximum = maximum_stack;
  bool heap = (u32)rows > 512;
  if (heap) {
    u32 storage = ((u32)rows * 4u) >> 2;
    assert(rows <= 0 || storage >= (u32)rows);
    minimum = new i32[storage];
    maximum = new i32[storage];
  }
  f32 left = (f32)(center.x - ((f64)center.x - source_rect->left) * scale);
  f32 top = (f32)(center.y - ((f64)center.y - source_rect->top) * scale);
  f32 right = (f32)(((f64)source_rect->right - center.x) * scale + center.x);
  f32 bottom = (f32)(((f64)source_rect->bottom - center.y) * scale + center.y);
  GxRotationPoint corners[4] = {{left, top}, {left, bottom}, {right, bottom}, {right, top}};
  f32 negative_angle = -angle;
  for (u32 index = 0; index < 4; ++index)
    GxRotatePoint(corners + index, center, negative_angle);
  f64 dx = (f64)half_width - center.x, dy = (f64)half_height - center.y;
  i32 offset_x = (i32)((u32)destination->left - (u32)rectangle.left);
  i32 offset_y = (i32)((u32)destination->top - (u32)rectangle.top);
  for (u32 index = 0; index < 4; ++index) {
    corners[index].x = (f32)((index ? (f64)(f32)offset_x : (f64)offset_x) + dx + corners[index].x);
    corners[index].y = (f32)((index ? (f64)(f32)dy : dy) + corners[index].y);
    corners[index].y = (f32)((f64)(f32)offset_y + corners[index].y);
  }
  GxRotationEdges(corners, rows, (i32)((u32)rectangle.right - (u32)rectangle.left), minimum, maximum);
  GxRotationPoint zero = {0, 0};
  u32 fixed_inverse = GxRotationInteger((f64)inverse * 65536.0);
  GxRotationFixed step = {fixed_inverse, 0};
  GxRotateFixed(&step, zero, angle);
  if (format[1] == 16 && source->Lock(GxLockFlags::Bits)) {
    bool keyed = source->GetTransparency();
    u32 target_pitch = GetColorSurfaceStride() >> 1, source_pitch = source->GetColorSurfaceStride() >> 1;
    u8 *target_data = static_cast<u8 *>(GetColorSurfaceBits()),
       *source_data = static_cast<u8 *>(source->GetColorSurfaceBits());
    u16 key = (u16)source->GetBltFlags()->pixel;
    for (i32 y = 0; y < rows; ++y) {
      i32 start = minimum[y], finish = maximum[y];
      i32 relative_x = (i32)((u32)start + (u32)rectangle.left - (u32)destination->left);
      i32 relative_y = (i32)((u32)y + (u32)rectangle.top - (u32)destination->top);
      GxRotationFixed position = {GxRotationInteger(((f64)relative_x - half_width + center.x) * 65536.0),
          GxRotationInteger(((f64)relative_y - half_height + center.y) * 65536.0)};
      GxRotateFixed(&position, center, angle);
      u32 cx = GxRotationInteger((f64)center.x * 65536.0);
      u32 cy = GxRotationInteger((f64)center.y * 65536.0);
      position.x = cx + GxRotationMultiply(position.x - cx, fixed_inverse) + 98;
      position.y = cy + GxRotationMultiply(position.y - cy, fixed_inverse) + 98;
      u32 target_row = 2u * ((u32)rectangle.top + (u32)y) * target_pitch;
      for (i32 x = start; x < finish; ++x) {
        u32 source_offset = 2u * ((u32)((i32)position.x >> 16) + source_pitch * (u32)((i32)position.y >> 16));
        u16 value;
        memcpy(&value, GxPixelAddress(source_data, source_offset), sizeof(value));
        if (!keyed || value != key)
          memcpy(GxPixelAddress(target_data, target_row + 2u * ((u32)rectangle.left + (u32)x)), &value, sizeof(value));
        position.x += step.x;
        position.y += step.y;
      }
    }
    source->Unlock(GxLockFlags::Bits);
  }
  if (heap) {
    delete[] minimum;
    delete[] maximum;
  }
  return 1;
}

// Forward the original five opacity-transfer arguments to the shared clipping/composition operation in source order.
u8 GxGeneralBuffer::AlphaBlt(GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input,
    f32 opacity, const ScRect *clip) {
  const ScRect *source_rect = &source_rect_input;
  const ScPoint *destination = &destination_input;
  return GxAlphaBltConstAlpha(source, this, opacity, *source_rect, *destination, clip);
}

// Forward the original four arguments to uniform opacity blending without adding clipping or normalizing its byte.
u8 GxGeneralBuffer::AlphaBltNoClip(
    GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) {
  const ScRect *source_rect = &source_rect_input;
  const ScPoint *destination = &destination_input;
  return GxAlphaBltConstAlphaNoClip(source, this, opacity, *source_rect, *destination);
}

// Execute the shared no-clip alpha body directly; precalculation introduces no separate state.
u8 GxGeneralBuffer::AlphaBltNoClipSourcePrecalculated(
    GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) {
  const ScRect *source_rect = &source_rect_input;
  const ScPoint *destination = &destination_input;
  return GxAlphaBltConstAlphaNoClip(source, this, opacity, *source_rect, *destination);
}

// Preserve the separate composition entry that shares the five-argument clipped opacity operation.
u8 GxGeneralBuffer::AlphaBltSourcePrecalculated(GxBuffer *source, const ScRect &source_rect_input,
    const ScPoint &destination_input, f32 opacity, const ScRect *clip) {
  const ScRect *source_rect = &source_rect_input;
  const ScPoint *destination = &destination_input;
  return GxAlphaBltConstAlpha(source, this, opacity, *source_rect, *destination, clip);
}

u8 GxGeneralBuffer::Fill(u32 pixel, ScRect *clip) {
  return Fill(pixel, *GetBufferArea(), clip);
}

u32 GxGeneralBuffer::GetBitsPerPixel() const {
  return format[1];
}

// Match depth allocation policy to the primary video-memory byte, then attach the child through the live primary.
u8 GxGeneralBuffer::InitSecondary() {
  u8 result =
      secondary_backend->Init((u32)area.right, (u32)area.bottom, &secondary_format, backend->video_memory ? 3 : 0);
  if (result)
    return backend->AddAttached(secondary_backend) != 0;
  return result;
}

// Write the genuine WORD output only for the six supported formats, leaving it unchanged on a zero return.
u8 GxGeneralBuffer::ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) {
  if (format[0] < 5 || format[0] > 10)
    return 0;
  *out = GxPackColor((u32)format[0], red, green, blue);
  return 1;
}

u8 GxGeneralBuffer::AlphaSourcePrecalculate(const ScRect &, f32) {
  return 1;
}

// Copy text then caption before checking fullscreen, flip to GDI when requested, and pause around the modal dialog.
// Resume through the live virtual method and return the complete Win32 result after both owned strings are destroyed.
u32 GxDisplay::MessageBoxA(const ScStringView &text, const ScStringView &caption) {
  ScString text_copy(text), caption_copy(caption);
  PauseDraw();
  i32 result = SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, caption_copy.Data(), text_copy.Data(), GxSdlWindow())
      ? 1 : 0;
  UnpauseDraw();
  return result;
}

GxCanvasRef::~GxCanvasRef() {
  if (value)
    value->Release();
}

GxDisplay::~GxDisplay() {
  if (GxGraphicsCurrent == static_cast<GxGraphics *>(this))
    GxGraphicsCurrent = nullptr;
}

GxColorType GxDisplay::GetScreenColorType() {
  return primary.GetColorType();
}

// Borrow the current application canvas without retaining it or manufacturing a window during lookup.
GxCanvas *GxDisplay::GetCanvas() const {
  return canvas.value;
}

// Return the actual embedded primary's secondary interface without creating a separate buffer owner.
GxPrimary *GxDisplay::ScreenBuffer() {
  return &primary;
}

u8 GxDisplay::IsFullScreenMode() {
  return fullscreen;
}

bool GxDisplay::DoesSystemHaveCursorSupport() {
  return hardware_cursor != 0;
}

u16 GxDisplay::ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) {
  return primary.ConvertRGBValueToNative16Bit(red, green, blue);
}

u8 GxDisplay::ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) {
  return primary.ConvertRGBValueToNative16Bit(out, red, green, blue);
}

u8 GxDisplay::Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  return primary.Convert16BitNativeValueToRGB(pixel, red, green, blue);
}

GxPackFunction GxDisplay::GetRGBTo16ConverterPtr() const {
  return primary.GetRGBTo16ConverterPtr();
}

GxUnpackFunction GxDisplay::Get16ToRGBConverterPtr() const {
  return primary.Get16ToRGBConverterPtr();
}

u8 GxDisplay::Convert16BitValueFromFormatToFormat(u16 *pixel, GxColorType::Color source, GxColorType::Color target) {
  return primary.Convert16BitValueFromFormatToFormat(pixel, static_cast<GxColorType::Color>(static_cast<u32>(source)),
      static_cast<GxColorType::Color>(static_cast<u32>(target)));
}

// Draw live listeners and the optional software cursor before presenting; a paused frame succeeds without touching the primary.
// Each canvas-coordinate callback completes before its live hotspot is read, and the vertical callback precedes final extents.
bool GxDisplay::Draw() {
  if (!pause) {
    GxBuffer *target = primary.ActiveDrawBuffer();
    if (target->IsReady()) {
      GxGraphicsBase::HookDraw();
      if (cursor && !suppress_overlay) {
        target = primary.ActiveDrawBuffer();
        ScRect source = {0, 0, cursor_width, cursor_height}, destination;
        i32 x = GxGetWindowManager()->GetCanvas()->LastCursorPositionX();
        destination.left = (i32)((u32)x - hotspot_x);
        i32 y = GxGetWindowManager()->GetCanvas()->LastCursorPositionY();
        destination.top = (i32)((u32)y - hotspot_y);
        destination.right = (i32)((u32)destination.left + cursor_width);
        destination.bottom = (i32)((u32)destination.top + cursor_height);
        if (target->Lock(GxLockFlags::Blt)) {
          target->Blt(cursor, source, destination, nullptr);
          target->Unlock(GxLockFlags::Blt);
        }
      }
    }
    primary.Swap();
  }
  return true;
}

// The first fullscreen pause returns the device to GDI before returning the live, potentially callback-mutated counter.
u32 GxDisplay::PauseDraw() {
  ++pause;
  if (fullscreen && pause == 1)
    primary.FlipToGDISurface();
  return pause;
}

// Mark shutdown before releasing listeners and the canvas, then leave fullscreen and dispose primary/device/adapter ownership.
// The borrowed manager HWND is not cleared by this source method; a canvas release callback still observes the old canvas field.
bool GxDisplay::Shutdown() {
  phase = 2;
  HookRelease();
  if (canvas.value)
    canvas.value->Release();
  canvas.value = nullptr;
  primary.Shutdown();
  ClearAdapters();
  return true;
}

bool GxDisplay::GetCanvas(u32 iid, void **out) {
  return canvas.value ? canvas.value->QueryInterface(iid, out) : false;
}

ScRect GxDisplay::CanvasRect() {
  return canvas_area;
}

// The second extraction reads the live level again after the first output may have changed it.
u8 GxDisplay::GetVideoDriverVersionInformation(u32 *major, u32 *minor) {
  *major = (capability_level >> 8) & 255;
  *minor = capability_level & 255;
  return 1;
}

// Copy the three stored timing floats in order without recomputing sample windows or touching either timer.
// Preserve the original void contract and three separate DWORD loads/stores, including overlapping output.
void GxDisplay::GetScreenTiming(GxTiming *out) {
  out->swap = timing.swap;
  out->window_manager = timing.window_manager;
  out->other = timing.other;
}

// Write zero until the 10-sample flag is set, then divide elapsed milliseconds into frames per second.
// Every addition is stored through the caller float before the next sample, preserving output aliasing and rounding.
void GxDisplay::GetRefreshRate(f32 *out) {
  *out = 0;
  if (flag652) {
    for (u32 index = 0; index < 10; ++index)
      *out = (f32)((f64)values10.data[index] + *out);
    *out = (f32)(10000.0 / *out);
  }
}

// Write zero until the 100-sample flag is set, then divide elapsed milliseconds into frames per second.
// Every addition is stored through the caller float before the next sample, preserving output aliasing and rounding.
void GxDisplay::GetRefreshRateAvg(f32 *out) {
  *out = 0;
  if (flag653) {
    for (u32 index = 0; index < 100; ++index)
      *out = (f32)((f64)values100.data[index] + *out);
    *out = (f32)(100000.0 / *out);
  }
}

// Write zero until the 1000-sample flag is set, then divide elapsed milliseconds into frames per second.
// Every addition is stored through the caller float before the next sample, preserving output aliasing and rounding.
void GxDisplay::GetRefreshRateAvgLong(f32 *out) {
  *out = 0;
  if (flag654) {
    for (u32 index = 0; index < 1000; ++index)
      *out = (f32)((f64)values1000.data[index] + *out);
    *out = (f32)(1000000.0 / *out);
  }
}

#pragma comment(lib, "ddraw.lib")

static GxCapabilities GxDisplayCapabilities = {};

// Construct the actual lock, adapter storage, primary and timers before publishing fullscreen policy and initialized
// float buffers.
GxDisplay::GxDisplay() {
  memset(&GxDisplayCapabilities, 0, sizeof(GxDisplayCapabilities));
  fullscreen = 1;
  timer1.SetResolution(GxTimeUnit::Microseconds);
  timer2.SetResolution(GxTimeUnit::Milliseconds);
  for (u32 index = 0; index < 10; ++index)
    *values10.Append() = 0;
  for (u32 index = 0; index < 100; ++index)
    *values100.Append() = 0;
  for (u32 index = 0; index < 1000; ++index)
    *values1000.Append() = 0;
}

bool GxDisplay::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GxDeviceManager, GxDeviceManager);
  return GxGraphicsBase::QueryInterface(iid, out);
}

u32 GxDisplay::AddRef() {
  return GxSystemServiceBase::AddRef();
}

u32 GxDisplay::Release() {
  return GxSystemServiceBase::Release();
}

// Borrow the actual module capability record; all display instances observe the same live DirectDraw outputs.
const GxCapabilities *GxDisplay::GetCapabilities() {
  return &GxDisplayCapabilities;
}

// Borrow this display's graphics identity from its Windows-manager view without querying or retaining either interface.
GxGraphics *GxDisplay::AsIGZGraphicSystem() {
  return static_cast<GxGraphics *>(this);
}

// The Windows backend advertises its constant two-dimensional acceleration support independently of device readiness.
bool GxDisplay::IsHardware2DAccellerated() {
  return false;
}

bool GxDisplay::IsHardware3DAccellerated() {
  return primary.IsInVideoMemory();
}

// Return the module's borrowed DirectDraw device without adding a COM reference.
SDL_Renderer *GxDisplay::GetDevice() {
  return GxSdlGetRenderer();
}

u8 GxDisplay::IsSoftware() {
  return software;
}

i32 GxDisplay::IsDefaultAdapter() {
  return memcmp(selected_adapter.guid, GxDefaultAdapter, 16) == 0;
}

// Update the software byte from the live selected GUID; nondefault adapters take the original software branch.
u8 GxDisplay::RefreshSoftware() {
  software = 0;
  if (memcmp(selected_adapter.guid, GxDefaultAdapter, 16))
    software = 1;
  return software;
}

bool GxDisplay::CreateDevice() {
  return GxSdlGetRenderer() != nullptr;
}

// Respect the virtual device query before releasing the live module pointer, then clear it after the COM callback.
bool GxDisplay::CloseDevice() {
  return GxSdlGetRenderer() != nullptr;
}

// Publish a newly constructed buffer before retaining its primary identity; allocation failure uses the global handler.
bool GxDisplay::CreateBuffer(GxBuffer **out) {
  *out = static_cast<GxBuffer *>(GxCreateBufferObject());
  (*out)->AddRef();
  return true;
}

void GxDisplay::ForceFullSystemRedraw() {
  GxGraphicsBase::HookForceFullSystemRedraw();
}

// Store the borrowed native handle and return that same pointer without querying or retaining the canvas.
SDL_Window *GxDisplay::SetWindowHandle(SDL_Window *window) {
  window_handle = window;
  return window;
}

// Borrow the current native handle directly from the graphics receiver's original manager state.
SDL_Window *GxDisplay::GetWindowHandle() {
  return window_handle;
}

// Apply1001250D's cooperative-level effects with the live device and HWND, preserving their virtual lookup order.
void GxDisplay::SetCooperativeLevel(u8) {
}

// Poll the live module device until its output BOOL or HRESULT is nonzero, retaining the output between calls.
// SDL cannot query the current blank interval, so its backend returns the standard unsupported HRESULT.
i32 GxDisplay::WaitForVerticalBlank() {
  constexpr i32 kGxVerticalBlankUnsupported = static_cast<i32>(0x80004001u);
  return kGxVerticalBlankUnsupported;
}

void GxDisplay::SetCooperativeLevel() {
  SetCooperativeLevel(fullscreen);
}

// Only successful transitions publish the incoming byte, refresh the primary when allowed, notify listeners and unlock.
void GxDisplay::SetFullscreen(u8 value) {
  if (value == IsFullScreenMode())
    return;
  draw_lock.Lock();
  if (!GxSdlSetWindowFullscreen(value != 0)) {
    draw_lock.Unlock();
    return;
  }
  fullscreen = value;
  if (value)
    HookCompleteSwitchToFullScreenMode();
  else
    HookCompleteSwitchToWindowedMode();
  draw_lock.Unlock();
}

// Compare the complete concrete switch value with an ASCII literal, including its stored length rather than its first NUL.
[[maybe_unused]] static bool GxSwitchEquals(const ScString &value, const char *text) {
  usize length = strlen(text);
  return value.Strlen() == length && memcmp(value.Data(), text, length) == 0;
}

// Interpret the external thirty-two-byte preference as a bounded string only after GUID matching has failed.
// Reject an unavailable out-of-object C-string read instead of copying that undefined source execution into native code.
[[maybe_unused]] static u32 GxPreferenceLength() {
  for (u32 length = 0; length < sizeof(GxAdapterPreference); ++length)
    if (!GxAdapterPreference[length])
      return length;
  ScFatal(SC_ERROR_UNSUPPORTED, "Unterminated graphics adapter preference");
}

// Search the concrete adapter description with the original empty-string rule and first matching position.
[[maybe_unused]] static bool GxMatchesPreference(const ScString &description, u32 length) {
  u32 size = description.Strlen();
  if (!size || length > size)
    return false;
  for (u32 index = 0; index <= size - length; ++index)
    if (memcmp(description.Data() + index, GxAdapterPreference, length) == 0)
      return true;
  return false;
}

// Initialize the actual adapter/device, command-line policies, canvas, primary surface and framework registrations in source order.
// Failed phases retain their published partial state for explicit Shutdown; private HWND-setter effects remain separately labelled.
bool GxDisplay::Init() {
  framework = GxFrameWork();
  fullscreen = 0;
  software = 1;
  if (GxRequestedMode.depth != 8 && GxRequestedMode.depth != 16 && GxRequestedMode.depth != 32)
    return false;
  GxUseCpuSurfaces(true);
  canvas_area = {0, 0, (i32)GxRequestedMode.width, (i32)GxRequestedMode.height};
  selected_mode = GxRequestedMode;
  if (canvas.value)
    canvas.value->Release();
  GxWindow *created = new GxWindow;
  created->AddRef();
  canvas.value = created;
  HookGSInit(primary.ActiveDrawBuffer());
  if (!canvas.value->Init(canvas_area))
    return false;
  SetWindowHandle(GxSdlWindow());
  if (!DiscoverAdapters() || !EnumerateModes() || adapters.head.next == &adapters.head)
    return false;
  selected_adapter = static_cast<GxAdapterNode *>(adapters.head.next)->adapter;
  for (GxAdapterLink *link = adapters.head.next; link != &adapters.head; link = link->next) {
    GxAdapter &adapter = static_cast<GxAdapterNode *>(link)->adapter;
    if (memcmp(GxAdapterPreference, adapter.guid, 16) == 0 ||
        GxMatchesPreference(adapter.description, GxPreferenceLength())) {
      selected_adapter = adapter;
      break;
    }
  }
  desktop_mode = GetScreenMetrics();
  canvas.value->SetMainBufferSurface(primary.ActiveDrawBuffer());
  canvas.value->ShowWindow();
  primary.SetGraphicSystem(static_cast<GxGraphics *>(this));
  GxColorType format(static_cast<GxColorType::Color>(GxRequestedMode.depth == 8 ? 3 : GxRequestedMode.depth == 32 ? 14 : 7));
  bool result = primary.Init(GxRequestedMode.width, GxRequestedMode.height, false, format, nullptr,
      GxDepthType(GxDepthType::Depth::None), GxPrimaryColorFrames, GxPrimaryDepthFrames, GxPrimaryReserveBytes);
  if (result) {
    phase = 0;
    GxFrameWork()->AddToTick(static_cast<ScSystemService *>(this));
    GxFrameWork()->AddHook(static_cast<ScFrameworkHooks *>(this));
  }
  return result;
}

// Emit the source diagnostic before restoring normal cooperative policy; abort notification does not call Shutdown.
bool GxDisplay::AbortiveQuit() {
  SetCooperativeLevel(0);
  return true;
}

// Report the original unavailable-query value when SDL exposes no video-memory budget.
// Rendering buffers remain CPU allocations; no hardware capacity is inferred from system RAM.
u32 GxDisplay::VideoMemory(GxVideoMemoryType, bool) {
  return 0;
}

// Construct a real general buffer and retain only the requested interface through its actual QueryInterface.
// A failed query deletes the unretained buffer and leaves output handling to the original query contract.
bool GxDisplay::CreateBuffer(u32 iid, void **out) {
  GxGeneralBuffer *buffer = new GxGeneralBuffer;
  if (buffer->QueryInterface(iid, out))
    return true;
  delete buffer;
  return false;
}

// Count the live adapter nodes on every call, including records appended during device callbacks.
// The original graphics108 entry forwards to its circular-list traversal without querying any device.
u32 GxDisplay::GetVideoDriverInfoCount() {
  u32 count = 0;
  for (GxAdapterLink *link = adapters.head.next; link != &adapters.head; link = link->next)
    ++count;
  return count;
}

// Preserve the original inverted index comparison and count-based walk, including its one-past-sentinel pointer.
// Returning that address reads no adapter payload; consumers must exclude the source's unavailable sentinel reads.
GxAdapter *GxDisplay::GetAdapter(u32 index) {
  if (adapters.head.next == &adapters.head)
    return nullptr;
  u32 count = 0;
  for (GxAdapterLink *link = adapters.head.next; link != &adapters.head; link = link->next)
    ++count;
  if (index < count)
    return nullptr;
  GxAdapterLink *link = adapters.head.next;
  for (; count; --count)
    link = link->next;
  return reinterpret_cast<GxAdapter *>(link + 1);
}

// Construct the temporary string before indexed lookup; defined null results leave both caller outputs untouched.
// The nonnull source result points past the list sentinel, whose adapter fields were never initialized.
u8 GxDisplay::GetVideoDriverInfo(ScStringView *, ScStringView *, u32 index) {
  ScString text;
  [[maybe_unused]] auto *adapter = GetAdapter(index);
  assert(!adapter);
  return 0;
}

// Borrow the owned adapter list through the manager receiver without changing records or device references.
GxAdapters *GxDisplay::GetAdapters() {
  return &adapters;
}

// Borrow the live selected adapter value, retaining its identity across assignment and device operations.
GxAdapter *GxDisplay::GetSelectedAdapter() {
  return &selected_adapter;
}

// Format the selected driver name and four version words before publishing either output string.
// Read the description after the first output callback, preserving changes made through the borrowed adapter.
u8 GxDisplay::GetVideoDriverInfoCurrent(ScStringView *driver, ScStringView *description) {
  ScString text;
  GxAdapter *adapter = GetSelectedAdapter();
  if (!adapter)
    return 0;
  text.Sprintf("%s, %d.%02d.%02d.%04d", adapter->name.begin, (i32)(adapter->version_high >> 16),
      (i32)(adapter->version_high & 65535), (i32)(adapter->version_low >> 16), (i32)(adapter->version_low & 65535));
  driver->FromChar(text.begin, (u32)(text.end - text.begin));
  description->FromChar(adapter->description.begin, (u32)(adapter->description.end - adapter->description.begin));
  return 1;
}

// Return a newly constructed unretained graphics service; the director's QueryInterface establishes its shared ownership.
ScUnknown *GxCreateDisplayObject() {
  return static_cast<ScSystemService *>(new GxDisplay);
}


void GxModes::Append(const GxMode &mode) {
  *rows.Append() = mode;
}

// Replace the live mode rows without changing their order. Self-assignment leaves both storage and values untouched.
void GxModes::Assign(const GxModes &source) {
  if (this == &source)
    return;
  rows.Clear();
  for (u32 index = 0; index < source.rows.size; ++index)
    Append(source.rows.data[index]);
}

// Compare only the source's three selection words in width/height/depth short-circuit order.
bool GxModes::Contains(const GxMode &mode) const {
  for (u32 index = 0; index < rows.size; ++index) {
    const GxMode &row = rows.data[index];
    if (row.width == mode.width && row.height == mode.height && row.depth == mode.depth)
      return true;
  }
  return false;
}


GxAdapters::GxAdapters() {
  head.next = head.previous = &head;
}

// Destroy list storage and strings without manufacturing device releases; the source Clear operation owns that pass.
GxAdapters::~GxAdapters() {
  while (head.next != &head) {
    GxAdapterNode *node = static_cast<GxAdapterNode *>(head.next);
    head.next = node->next;
    delete node;
  }
}

// Append one native adapter record for the actual discovery code to populate, without adding a COM reference.
GxAdapter *GxAdapters::Append() {
  GxAdapterNode *node = new GxAdapterNode;
  node->previous = head.previous;
  node->next = &head;
  head.previous->next = node;
  head.previous = node;
  return &node->adapter;
}

// Find the first byte-equal sixteen-byte adapter identity, as the original graphics mode lookup does.
GxAdapter *GxAdapters::Find(const u8 *guid) {
  for (GxAdapterLink *link = head.next; link != &head; link = link->next) {
    GxAdapter &adapter = static_cast<GxAdapterNode *>(link)->adapter;
    if (!memcmp(adapter.guid, guid, 16))
      return &adapter;
  }
  return nullptr;
}

// A missing or empty adapter never reads the mode.
bool GxAdapters::Supports(const GxMode *mode, const u8 *guid) {
  GxAdapter *adapter = Find(guid);
  if (!adapter || !adapter->modes.rows.size)
    return false;
  return adapter->modes.Contains(*mode);
}

void GxAdapters::Clear() {
  while (head.next != &head) {
    GxAdapterNode *node = static_cast<GxAdapterNode *>(head.next);
    node->next->previous = &head;
    head.next = node->next;
    delete node;
  }
}

#undef RGB


// Bind the platform owner directly; module QueryInterface ownership belongs to the original SetGraphicSystem method.
GxSoftwarePrimary::GxSoftwarePrimary(GxGraphics *owner, u32 native_format) {
  graphics = owner;
  format[0] = static_cast<i32>(native_format);
  format[1] = 16;
}

GxSoftwarePrimary::~GxSoftwarePrimary() {
  Shutdown();
}

// Allocate the real CPU pixel plane using the mode already selected by the original primary initialization body.
u8 GxSoftwarePrimary::CreateScreenSurfaces() {
  backend = GxCreateSurface();
  backend->owner = this;
  if (!backend->Init((u32)area.right, (u32)area.bottom, format, 1, 0))
    return 0;
  front = backend;
  return 1;
}

// Release native pixel ownership, then clear the primary aliases after the actual backend cleanup.
u8 GxSoftwarePrimary::DestroyScreenSurfaces() {
  initialized = false;
  CloseBackends();
  front = nullptr;
  draw_target = nullptr;
  return 1;
}

// CPU presentation uses its actual owned pixel plane, without an emulated DirectDraw flipping chain.
GxDrawSurface *GxSoftwarePrimary::GetDisplaySurface() {
  return backend;
}

// Preserve the original presentation-suppression success byte before entering the native CPU display boundary.
u8 GxSoftwarePrimary::Swap() {
  if (present_depth)
    return 1;
  if (!initialized)
    return 0;
  return GxSdlPresent(static_cast<const u8 *>(GetColorSurfaceBits()), GetColorSurfaceStride(), Width(), Height(),
      format[0], format[1], palette);
}
