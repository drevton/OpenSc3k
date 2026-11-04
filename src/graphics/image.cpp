#include "graphics/image.h"

#include <cassert>
#include <cstring>
#include <new>

#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "graphics/api.h"
#include "graphics/module.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_effects.h"

// Retain the original registered service priority and its startup/shutdown ordering.
enum GxColorPriority : i32 {
  kGxColorServicePriority = -1000000,
};

// Encoded image rows store their pixel count and opaque-run flag in the same WORD.
enum GxImageRowBits : u32 {
  kGxImageRowLengthMask = 0x7fff,
  kGxImageRowOpaque = 0x8000,
};

GxBlendFunction GxBlendColor = GxBlendColor565;
GxGammaFunction GxGammaColor = GxGammaColor565;

u32 GX_COLOR_CALL GxBlendColor555(u32 source, u32 destination, i32 amount) {
  return GxBlend555(source, destination, (u32)amount);
}

u32 GX_COLOR_CALL GxBlendColor565(u32 source, u32 destination, i32 amount) {
  return GxBlend565(source, destination, (u32)amount);
}

// Transform the three RGB555 channels through the actual gamma table; callers supply defined table indexes.
u32 GX_COLOR_CALL GxGammaColor555(u32 pixel, i32 amount) {
  u32 red = 32 * (pixel >> 10) + (u32)amount, green = 32 * ((pixel >> 5) & 31) + (u32)amount;
  u32 blue = 32 * (pixel & 31) + (u32)amount;
  assert(red < sizeof(GxGamma) && green < sizeof(GxGamma) && blue < sizeof(GxGamma));
  return GxGamma[blue] | (32u * (GxGamma[green] | (32u * GxGamma[red])));
}

// Preserve the original RGB565 five-bit green lookup and six-bit output shift when applying gamma.
u32 GX_COLOR_CALL GxGammaColor565(u32 pixel, i32 amount) {
  u32 red = 32 * (pixel >> 11) + (u32)amount, green = 32 * ((pixel >> 6) & 31) + (u32)amount;
  u32 blue = 32 * (pixel & 31) + (u32)amount;
  assert(red < sizeof(GxGamma) && green < sizeof(GxGamma) && blue < sizeof(GxGamma));
  return GxGamma[blue] | (64u * (GxGamma[green] | (32u * GxGamma[red])));
}

// Select the two actual module dispatch pointers for RGB555 or RGB565; unsupported formats leave both untouched.
bool GxSelectColorFunctions(const i32 *format) {
  if (format[0] == 5) {
    GxBlendColor = GxBlendColor555;
    GxGammaColor = GxGammaColor555;
    return true;
  }
  if (format[0] == 7) {
    GxBlendColor = GxBlendColor565;
    GxGammaColor = GxGammaColor565;
    return true;
  }
  return false;
}

GxColorService::GxColorService() : GxSystemServiceBase(kGxColorFunctionsServiceId, kGxColorServicePriority) {
}

// Publish the color-function identity before retaining the primary service; other requests use the original base.
bool GxColorService::QueryInterface(u32 iid, void **out) {
  if (iid != IID_GxColorFunctions)
    return GxSystemServiceBase::QueryInterface(iid, out);
  *out = static_cast<GxColorFunctions *>(this);
  static_cast<ScSystemService *>(this)->AddRef();
  return true;
}

u32 GxColorService::AddRef() {
  return GxSystemServiceBase::AddRef();
}

u32 GxColorService::Release() {
  return GxSystemServiceBase::Release();
}

bool GxColorService::Init(const i32 *format) {
  return GxSelectColorFunctions(format);
}

GxFrameList::GxFrameList() : head(new Node) {
  head->next = head;
  head->previous = head;
}

GxFrameList::~GxFrameList() {
  ClearRaw();
  delete head;
}

// Find the first matching live interface pointer in the source registration order.
GxFrameList::Node *GxFrameList::Find(GxFrameListener *listener) {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    if (node->listener == listener)
      return node;
  return end;
}

// Unlink and destroy exactly the captured node; valid callbacks must not have already erased that source iterator.
void GxFrameList::Erase(Node *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

// Reentrant insertion after the duplicate scan can produce valid duplicates.
bool GxFrameList::Add(GxFrameListener *listener) {
  if (!listener || Find(listener) != head)
    return false;
  listener->AddRef();
  Node *node = new Node{head, head->previous, listener};
  node->previous->next = node;
  head->previous = node;
  return true;
}

bool GxFrameList::Remove(GxFrameListener *listener) {
  Node *node = Find(listener);
  if (node == head)
    return false;
  if (node->listener)
    node->listener->Release();
  Erase(node);
  return true;
}

void GxFrameList::GSInit(GxGraphics *graphics, GxBuffer *buffer) {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->GSInit(graphics, buffer);
}

// Deliver the original refresh phase without promoting an empty traversal's undefined register residue into a result.
void GxFrameList::ForceFullSystemRedraw() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->ForceFullSystemRedraw();
}

void GxFrameList::Draw() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->Draw();
}

// Deliver the source window-move phase through each live listener's actual virtual method.
void GxFrameList::ProcessWindowMove() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->ProcessWindowMove();
}

// Deliver the fullscreen transition without snapshotting the list across callbacks.
void GxFrameList::CompleteSwitchToFullScreenMode() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->CompleteSwitchToFullScreenMode();
}

// Deliver the windowed transition with the original live next-node reload.
void GxFrameList::CompleteSwitchToWindowedMode() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->CompleteSwitchToWindowedMode();
}

// Release the whole live sequence first, then raw-erase the list; this deliberately differs from repeated Remove calls.
void GxFrameList::ReleaseAll() {
  Node *end = head;
  for (Node *node = head->next; node != end; node = node->next)
    node->listener->Release();
  ClearRaw();
}

void GxFrameList::ClearRaw() {
  while (head->next != head)
    Erase(head->next);
}

static u16 GxImageWord(const u8 *bytes) {
  return (u16)(bytes[0] | ((u16)bytes[1] << 8));
}

static u32 GxImageDword(const u8 *bytes) {
  return bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

static void GxImagePutWord(u8 *bytes, u16 value) {
  bytes[0] = (u8)value;
  bytes[1] = (u8)(value >> 8);
}

static void GxImagePutDword(u8 *bytes, u32 value) {
  for (u32 index = 0; index < 4; ++index)
    bytes[index] = (u8)(value >> (8 * index));
}

static u16 GxImageConvert(u16 pixel, u32 source) {
  return source == 5 ? (u16)(pixel + (pixel & 0xffe0u)) : (u16)((pixel & 31u) | ((pixel >> 1) & 0x7fe0u));
}

// Initialize the actual owned block, independent bounds, two color keys and initially disabled reference counting.
GxImage::GxImage() = default;

GxImage::~GxImage() {
  Shutdown();
}

u8 GxImage::SetAntialiasingOfBoundingPixels(bool value) {
  clipped = value;
  return value;
}

// Borrow the primary buffer identity from the builder receiver without querying or adding an ownership reference.
GxBuffer *GxImage::AsIGZBuffer() {
  return static_cast<GxBuffer *>(this);
}

// Publish the buffer or separate builder identity before enabling/incrementing shared reference counting.
bool GxImage::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxBuffer);
  HANDLE_QUERY_INTERFACE(IID_GxBuffer, GxBuffer);
  HANDLE_QUERY_INTERFACE(IID_GxImageBuilder, GxImageBuilder);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Enable counting even when the increment wraps the actual reference word to zero.
u32 GxImage::AddRef() {
  ++references;
  counting_enabled = true;
  return references;
}

// Delete an uncounted image or an entry-count-one image; enabled zero decrements with DWORD wrapping.
u32 GxImage::Release() {
  if (!counting_enabled || references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

bool GxImage::Init(u32, u32, GxColorType, GxPaletteView *, GxDepthType, GxVramRequest, bool) {
  return true;
}

// Free the current block and clear only its pointer, leaving the saved bounds and keys intact.
bool GxImage::Shutdown() {
  delete[] data;
  data = nullptr;
  return true;
}

// Dispose a present image block and clear its pointer, retaining the saved bounds and color keys.
bool GxImage::Uninitialize() {
  if (data) {
    delete[] data;
    data = nullptr;
  }
  return true;
}

u8 GxImage::Lock(GxLockFlags) {
  return 1;
}

u8 GxImage::Unlock(GxLockFlags) {
  return 1;
}

u8 GxImage::LockDepthSurface(GxLockFlags) {
  return 1;
}

u8 GxImage::UnlockDepthSurface(GxLockFlags) {
  return 1;
}

bool GxImage::IsLocked() const {
  return true;
}

bool GxImage::IsDepthSurfaceLocked() const {
  return true;
}

u32 GxImage::CompareToBuffer(GxBuffer *) {
  return 0;
}

u32 GxImage::CompareZToZBuffer(GxBuffer *) {
  return 0;
}

u32 GxImage::GetZValue(u32, u32) const {
  return 0;
}

void GxImage::SetZValue(u32, u32, u32) {
}

void GxImage::EnableZBufferBlit(GxBltFlags::Options) {
}

void GxImage::DisableZBufferBlit() {
}

bool GxImage::IsInVideoMemory() const {
  return false;
}

u32 GxImage::GetBytesPerPixel() const {
  return 2;
}

u8 GxImage::IsTexture() const {
  return 0;
}

u32 GxImage::GetMemoryUsage() const {
  return data ? GxImageDword(data) + 4u : 4u;
}

// The compressed image exposes no raw secondary pixel plane through this interface.
void *GxImage::GetDepthSurfaceBits() {
  return nullptr;
}

u32 GxImage::GetDepthSurfaceStride() {
  return 0;
}

bool GxImage::IsReady() {
  return data != nullptr;
}

u32 GxImage::Width() const {
  return data ? GxImageWord(data + 4) : 0;
}

u32 GxImage::Height() const {
  return data ? GxImageWord(data + 6) : 0;
}

// Borrow the actual saved rectangle, which can remain populated after block shutdown.
const ScRect *GxImage::GetBufferArea() const {
  return &area;
}

void GxImage::GetBufferArea(ScRect *out) const {
  *out = area;
}

void GxImage::GetColorType(GxColorType *out) const {
  *out = GxColorType(static_cast<GxColorType::Color>((i16)GxImageWord(data + 10)));
}

GxColorType GxImage::GetColorType() const {
  return GxColorType(static_cast<GxColorType::Color>((i16)GxImageWord(data + 10)));
}

GxDepthType GxImage::GetZDepth() const {
  return GxDepthType(GxDepthType::Depth::None);
}

// Borrow the real three-word color record, separate from the current out-of-span pixel key.
const GxBltFlags *GxImage::GetBltFlags() const {
  return &color;
}

void GxImage::GetBltFlags(GxBltFlags *out) const {
  *out = color;
}

u16 GxImage::ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) {
  return GxGetGraphics()->ConvertRGBValueToNative16Bit(red, green, blue);
}

u8 GxImage::Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  return GxGetGraphics()->Convert16BitNativeValueToRGB(pixel, red, green, blue);
}

// Resolve the live graphics primary and its buffer before requesting the actual packer; either absent view returns null.
GxPackFunction GxImage::GetRGBTo16ConverterPtr() const {
  GxPrimary *primary = GxGetGraphics()->ScreenBuffer();
  if (!primary)
    return nullptr;
  GxBuffer *buffer = primary->AsIGZBuffer();
  return buffer ? buffer->GetRGBTo16ConverterPtr() : nullptr;
}

// Query the current primary buffer independently for the unpacker, retaining the source's separate getter ordering.
GxUnpackFunction GxImage::Get16ToRGBConverterPtr() const {
  GxPrimary *primary = GxGetGraphics()->ScreenBuffer();
  if (!primary)
    return nullptr;
  GxBuffer *buffer = primary->AsIGZBuffer();
  return buffer ? buffer->Get16ToRGBConverterPtr() : nullptr;
}

// Resolve a cropped row's live span, returning the current key outside it and native WORD/BYTE payload inside.
u32 GxImage::GetPixel(u32 x, u32 y) const {
  u32 column = x, row_index = y;
  if (column >= GxImageWord(data + 4))
    return key;
  u32 height = GxImageWord(data + 6);
  if (row_index >= height)
    return key;
  const u8 *row = data + 16 + 8 * row_index;
  u32 leading = GxImageWord(row + 4);
  if (column < leading)
    return key;
  u32 length = GxImageWord(row + 6) & kGxImageRowLengthMask;
  if (column >= leading + length)
    return key;
  bool word = GxImageWord(data + 8) == 4;
  u32 offset = GxImageDword(row) + column - leading;
  const u8 *pixels = data + 16 + 8 * height;
  return word ? GxImageWord(pixels + 2 * offset) : pixels[offset];
}

// Store the low WORD or BYTE inside the encoded row, preserving the original header-relative offset.
// Out-of-span coordinates have no effects; the original DWORD-input method returns void.
void GxImage::SetPixel(u32 x, u32 y, u32 pixel) {
  u32 column = x, row_index = y;
  if (column >= GxImageWord(data + 4) || row_index >= GxImageWord(data + 6))
    return;
  const u8 *row = data + 16 + 8 * row_index;
  u32 leading = GxImageWord(row + 4);
  if (column < leading)
    return;
  u32 length = GxImageWord(row + 6) & kGxImageRowLengthMask;
  if (column >= leading + length)
    return;
  bool word = GxImageWord(data + 8) == 4;
  u32 offset = GxImageDword(row);
  if (word)
    GxImagePutWord(data + offset + 2 * (column - leading), (u16)pixel);
  else
    data[offset + column - leading] = (u8)pixel;
}

void GxImage::SetPixelAlpha(u32, u32, u32, f32) {
}

// The compressed image has no raw driver pixel plane; source slot424 returns a genuine null pointer.
void *GxImage::GetColorSurfaceBits() {
  return nullptr;
}

u32 GxImage::GetColorSurfaceStride() {
  return 0;
}

// The source image ignores palette exchange and returns no previous palette.
GxPaletteView *GxImage::ReplacePalette(GxPaletteView *) {
  return nullptr;
}

// Resolve target one through the graphics service, then perform the image class's original two-way WORD conversion.
u8 GxImage::Convert16BitValueFromFormatToFormat(
    u16 *pixel, GxColorType::Color source_value, GxColorType::Color target_value) {
  u32 source = static_cast<u32>(source_value);
  u32 target = static_cast<u32>(target_value);
  if (target == 1)
    target = static_cast<u32>(GxGetGraphics()->GetScreenColorType().color);
  if (source != target)
    *pixel = GxImageConvert(*pixel, source);
  return 1;
}

// Convert category-four blocks after changing format/key; use the source byte offset to locate the first converted WORD.
u8 GxImage::Convert16BitBufferFromFormatToFormat(GxColorType::Color source_value, GxColorType::Color target_value) {
  u32 source = static_cast<u32>(source_value);
  u32 target = static_cast<u32>(target_value);
  if (GxImageWord(data + 8) != 4)
    return 1;
  if (target == 1)
    target = static_cast<u32>(GxGetGraphics()->GetScreenColorType().color);
  if (source == target)
    return 1;
  uptr position = (uptr)data + 16 + 8 * (u32)GxImageWord(data + 6) + GxImageDword(data + 16);
  uptr end = (uptr)data + GxImageDword(data);
  GxImagePutWord(data + 10, (u16)target);
  key = GxImageConvert((u16)key, source);
  while (position < end) {
    u8 *pixel = reinterpret_cast<u8 *>(position);
    GxImagePutWord(pixel, GxImageConvert(GxImageWord(pixel), source));
    position += 2;
  }
  return 1;
}

u8 GxImage::Blt(GxBuffer *, const ScRect &, const ScRect &, const ScRect *) {
  return 0;
}

u8 GxImage::Blt(GxBuffer *, const ScRect &, const ScRect &, const GxBltFlags &) {
  return 0;
}

u8 GxImage::BltNoClip(GxBuffer *, const ScRect &, const ScRect &, const GxBltFlags &) {
  return 0;
}

u8 GxImage::BltStretch(GxBuffer *, const ScRect &, const ScRect &, const ScRect *) {
  return 0;
}

u8 GxImage::BltStretchNoClip(GxBuffer *, const ScRect &, const ScRect &) {
  return 0;
}

u8 GxImage::BltStretch(GxBuffer *, const ScRect &, const ScRect &, const GxBltFlags &) {
  return 0;
}

u8 GxImage::BltStretchNoClip(GxBuffer *, const ScRect &, const ScRect &, const GxBltFlags &) {
  return 0;
}

// Compressed images expose no palette object through their buffer identity.
GxPaletteView *GxImage::GetPalette() const {
  return nullptr;
}

u8 GxImage::BltBlurNoClip(GxBuffer *, const ScRect &, const ScPoint &, f32) {
  return 0;
}

u8 GxImage::DrawLineNoClip(i32, i32, i32, i32, i32, u32) {
  return 0;
}

u8 GxImage::DrawLineAlpha(i32, i32, i32, i32, i32, u32, f32) {
  return 0;
}

u8 GxImage::DrawLineNoClipAlpha(i32, i32, i32, i32, i32, u32, f32) {
  return 0;
}

u8 GxImage::DrawRectangleOutlineNoClip(const ScRect &, u32, u32) {
  return 0;
}

u8 GxImage::FillCircle(i32, i32, i32, i32, u32) {
  return 0;
}

u8 GxImage::FillCircleNoClip(i32, i32, i32, i32, u32) {
  return 0;
}

u8 GxImage::FloodFill(i32, i32, u32) {
  return 0;
}

u8 GxImage::FloodFillNoClip(i32, i32, u32) {
  return 0;
}

u8 GxImage::BoundaryFill(i32, i32, u32, u32) {
  return 0;
}

u8 GxImage::BoundaryFillNoClip(i32, i32, u32, u32) {
  return 0;
}

u8 GxImage::DrawCircle(i32, i32, i32, i32, u32, u32) {
  return 0;
}

u8 GxImage::DrawCircleNoClip(i32, i32, i32, i32, u32, u32) {
  return 0;
}

u8 GxImage::BltTiledNoClip(GxBuffer *, const ScRect &, const ScRect &, i32, i32) {
  return 0;
}

u8 GxImage::AlphaBltNoClip(GxBuffer *, const ScRect &, const ScPoint &, f32) {
  return 0;
}

u8 GxImage::AlphaBltNoClipSourcePrecalculated(GxBuffer *, const ScRect &, const ScPoint &, f32) {
  return 0;
}

u8 GxImage::RotateBlt(GxBuffer *, const ScRect &, const ScRect &, f32, const ScRect *) {
  return 0;
}

u8 GxImage::RotateBltStretch(GxBuffer *, const ScRect &, const ScRect &, f32, f32, const ScRect *) {
  return 0;
}

u8 GxImage::AlphaBltSourcePrecalculated(GxBuffer *, const ScRect &, const ScPoint &, f32, const ScRect *) {
  return 0;
}

// Let the destination clip the rectangles, then preserve the raw result of this image's virtual transfer core.
u8 GxImage::BltSrc(
    GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination_box = &destination_box_input;
  ScRect sr = *source_box, dr = *destination_box;
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  return BltNoClipSrc(destination, sr, dr);
}

// Decode cropped WORD rows into the destination plane, preserving key holes and the optional blended span edges.
// The image block is captured before destination callbacks; row count follows the destination rectangle.
u8 GxImage::BltNoClipSrc(GxBuffer *destination, const ScRect &sr_input, const ScRect &dr_input) {
  const ScRect *sr = &sr_input;
  const ScRect *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 stride = destination->GetColorSurfaceStride() >> 1;
  i32 width = sr->right - sr->left, top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)top * stride + dr->left);
  bool blend = clipped;
  i32 height = dr->bottom - top, source_top = sr->top;
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)source_top + y);
    u32 offset = GxImageDword(row), leading = GxImageWord(row + 4), record = GxImageWord(row + 6);
    u32 length = record & kGxImageRowLengthMask, end = leading + length;
    if (sr->left >= (i32)end || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    i32 count = leading <= (u32)sr->left ? width : sr->right - (i32)leading;
    if ((u32)count > length - advance)
      count = (i32)(length - advance);
    const u8 *input = payload + 2 * (offset + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    if (blend && !advance) {
      GxImagePutWord(target, (u16)GxBlendColor(GxImageWord(input), GxImageWord(target), 16));
      input += 2;
      target += 2;
      --count;
    }
    if (blend && count > 0 && sr->right >= (i32)end) {
      GxImagePutWord(target + 2 * (count - 1),
          (u16)GxBlendColor(GxImageWord(input + 2 * ((iptr)count - 1)), GxImageWord(target + 2 * (count - 1)), 16));
      --count;
    }
    if (record & kGxImageRowOpaque)
      memcpy(target, input, 2 * (usize)count);
    else {
      for (i32 x = 0; x < count; ++x) {
        u16 pixel = GxImageWord(input + 2 * x);
        if (pixel != key)
          GxImagePutWord(target + 2 * x, pixel);
      }
    }
  }
  return 1;
}

// Clip through the destination's scaled entry before submitting the image's virtual raster core.
// Twofold horizontal expansion temporarily trims an unmatched bottom row, restoring it only under the source clip rule.
u8 GxImage::BltStretchSrc(
    GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination_box = &destination_box_input;
  ScRect sr = *source_box, dr = *destination_box;
  if ((i32)((u32)sr.right - (u32)sr.left) != (i32)((u32)dr.right - (u32)dr.left) / 2) {
    if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
      return 0;
    return BltStretchNoClipSrc(destination, sr, &dr);
  }
  bool trimmed = 2u * ((u32)sr.bottom - (u32)sr.top) != (u32)dr.bottom - (u32)dr.top;
  i32 bottom = dr.bottom;
  if (trimmed)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (trimmed && (u32)bottom == (u32)dr.bottom + 1 && (!clip || (bottom >= clip->top && bottom <= clip->bottom)))
    dr.bottom = (i32)((u32)dr.bottom + 1);
  return BltStretchNoClipSrc(destination, sr, &dr);
}

// Expand cropped WORD rows by two or four, preserving transparency and the source's mutable odd-edge rectangle effects.
// Row groups retain the original DWORD-rounded inner pitch and extra odd-height writes; callers provide defined storage.
u8 GxImage::BltStretchNoClipSrc(GxBuffer *destination, const ScRect &sr_input, ScRect *dr) {
  const ScRect *sr = &sr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 stride = destination->GetColorSurfaceStride() >> 1;
  i32 source_width = (i32)((u32)sr->right - (u32)sr->left), left = dr->left, top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)top * stride + left);
  u32 height = (u32)dr->bottom - (u32)top;
  u8 *end = output + 2 * (usize)stride * height;
  bool odd_width = ((u32)dr->right - (u32)left) & 1, odd_height = height & 1;
  if (odd_width)
    dr->right = (i32)((u32)dr->right - 1);
  if (odd_height)
    dr->bottom = (i32)((u32)dr->bottom - 1);
  u32 width = (u32)dr->right - (u32)left;
  u32 scale = width == 2u * (u32)source_width ? 2 : width == 4u * (u32)source_width ? 4 : 0;
  if (!scale || !stride)
    return 1;
  const u8 *row = block + 16 + 8 * (iptr)sr->top;
  u32 inner_pitch = 4 * (stride >> 1);
  for (u8 *group = output; group < end; group += 2 * (usize)scale * stride, row += 8) {
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? (u32)source_width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = group + 2 * scale * (usize)(advance ? 0 : leading - (u32)sr->left);
    for (u32 index = 0; index < count; ++index) {
      u16 pixel = GxImageWord(input + 2 * index);
      if ((record & kGxImageRowOpaque) || pixel != key)
        for (u32 y = 0; y < scale; ++y)
          for (u32 x = 0; x < scale; ++x)
            GxImagePutWord(target + (usize)y * inner_pitch + 2 * (scale * index + x), pixel);
    }
    if (scale == 2) {
      if (odd_width) {
        u16 pixel = GxImageWord(input + 2 * ((iptr)count - 1));
        if ((record & kGxImageRowOpaque) || pixel != key) {
          GxImagePutWord(target + 4 * count, pixel);
          GxImagePutWord(target + 2 * (usize)stride + 4 * count, pixel);
        }
      }
      if (odd_height && group + 4 * (usize)stride >= end) {
        for (u32 index = 0; index < count; ++index) {
          u16 pixel = GxImageWord(input + 2 * index);
          if (pixel != key) {
            GxImagePutWord(end + 4 * index, pixel);
            GxImagePutWord(end + 4 * index + 2, pixel);
          }
        }
        if (odd_width)
          GxImagePutWord(end + 4 * count, GxImageWord(input + 2 * ((iptr)count - 1)));
      }
    }
  }
  return 1;
}

u8 GxImage::BltBlur(GxBuffer *, const ScRect &, const ScPoint &, f32, const ScRect *) {
  return 0;
}

void GxImage::SetTransparency(u32 value) {
  key = value;
  color.pixel = value;
}

u8 GxImage::GetTransparentColor(u32 *out) const {
  *out = color.pixel;
  return true;
}

u8 GxImage::FillNoClip(u32) {
  return 0;
}

u8 GxImage::ZFill(u32) {
  return 0;
}

u8 GxImage::FillNoClip(u32, const ScRect &) {
  return 0;
}

u8 GxImage::FillNoClip(u32, i32, i32, i32, i32) {
  return 0;
}

u8 GxImage::ZFill(u32, const ScRect &) {
  return 0;
}

u8 GxImage::Fill(u32, i32, i32, i32, i32, ScRect *) {
  return 0;
}

u8 GxImage::Fill(u32, const ScRect &, ScRect *) {
  return 0;
}

u8 GxImage::GenericBufferClip(GxBuffer *receiver, ScRect *source_rect, ScRect *destination, const ScRect *clip) {
  return receiver->GenericBufferClip(receiver, source_rect, destination, clip);
}

u8 GxImage::GenericBufferClipStretch(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip,
    f32 *horizontal_scale, f32 *vertical_scale) {
  return receiver->GenericBufferClipStretch(receiver, source, destination, clip, horizontal_scale, vertical_scale);
}

u8 GxImage::AlphaFill(u32, const ScRect &, f32, const ScRect *) {
  return 0;
}

u8 GxImage::AlphaFillNoClip(u32, const ScRect &, f32) {
  return 0;
}

u8 GxImage::BltTiled(GxBuffer *, const ScRect &, const ScRect &, i32, i32, const ScRect *) {
  return 0;
}

u8 GxImage::DrawLine(i32, i32, i32, i32, i32, u32) {
  return 0;
}

u8 GxImage::DrawRectangleOutline(const ScRect &, u32, u32) {
  return 0;
}

u8 GxImage::DrawRaisedRect(const ScRect &, u32, bool, bool) {
  return false;
}

u8 GxImage::DrawRaisedRect(const ScRect &, u8, u8, u8, bool, bool) {
  return false;
}

u8 GxImage::GetTransparency() const {
  return true;
}

u8 GxImage::IsPixelTransparent(u32 x, u32 y) {
  u32 pixel = GetPixel((i32)x, (i32)y);
  return pixel == color.pixel;
}

void GxImage::RemoveTransparency() {
  key = color.pixel = 0;
}

void GxImage::SetBltFlags(const GxBltFlags &value_input) {
  const GxBltFlags *value = &value_input;
  color.pixel = value->pixel;
}

// Transfer or copy the block before reading its header, without freeing a displaced allocation.
// Reload the live block after virtual key assignment before reading height and width.
bool GxImage::ImportFromData(u8 *allocation, bool take) {
  if (take)
    data = allocation;
  else {
    u32 size = GxImageDword(allocation);
    data = new u8[size];
    memcpy(data, allocation, size);
  }
  if (data) {
    SetTransparency(GxImageWord(data + 12));
    u32 height = GxImageWord(data + 6), width = GxImageWord(data + 4);
    area = {0, 0, (i32)width, (i32)height};
  }
  return true;
}

// Publish the genuine length output, allocate a fresh copy and publish its pointer before copying the live block.
bool GxImage::ExportToData(u8 **allocation, u32 *size) {
  *size = GxImageDword(data);
  u8 *copy = new u8[*size];
  *allocation = copy;
  memcpy(copy, data, *size);
  return true;
}

// Clear the old block through the original virtual shutdown, encode into the live field and then reload its key WORD.
bool GxImage::ConvertStandardBufferToData(GxBuffer *source) {
  Shutdown();
  u32 size;
  if (!ConvertStandardBufferToData(source, &data, &size))
    return false;
  SetTransparency(GxImageWord(data + 12));
  return true;
}

// Actual eight-byte row record in the compressed image format; offsets count pixels rather than bytes.
struct GxImageRow {
  u32 offset;
  u16 leading, length;
};

static u32 GxImageSourcePixel(const u8 *row, u32 column, u32 unit) {
  return unit == 2 ? GxImageWord(row + 2 * column) : row[column];
}

// Crop each source row's key-colored edges, retain internal key holes and publish a real header/payload allocation.
// Pixel pointer and pitch are fetched per row; final format lookup precedes publication and live source unlock.
bool GxImage::ConvertStandardBufferToData(GxBuffer *source, u8 **allocation, u32 *size) {
  if (!source->Lock(GxLockFlags::Bits))
    return false;
  u32 unit = source->GetColorType().depth == 16 ? 2u : 1u;
  u32 width = source->Width(), height = source->Height();
  GxBltFlags input_color = {};
  source->GetBltFlags(&input_color);
  u32 input_key = input_color.pixel, match = input_key & (unit == 2 ? 0xffffu : 0xffu), count = 0;
  ScArray<GxImageRow> rows;
  ScArray<u8> pixels;
  area = {0, 0, (i32)width, (i32)height};
  for (u32 y = 0; y < height; ++y) {
    u8 *bytes = static_cast<u8 *>(source->GetColorSurfaceBits());
    u32 pitch = source->GetColorSurfaceStride(), row_offset = y * pitch;
    const u8 *row = bytes + (unit == 2 ? row_offset & ~1u : row_offset);
    u32 leading = 0, trailing = 0, run = 0;
    bool opaque = true;
    while (leading < width && GxImageSourcePixel(row, leading, unit) == match)
      ++leading;
    if (leading < width) {
      while (GxImageSourcePixel(row, width - trailing - 1, unit) == match)
        ++trailing;
      run = width - trailing - leading;
      for (u32 x = leading; x < leading + run; ++x)
        if (GxImageSourcePixel(row, x, unit) == match) {
          opaque = false;
          break;
        }
    }
    *rows.Append() = {count, (u16)leading, (u16)(run | (opaque ? kGxImageRowOpaque : 0))};
    count += run;
    for (u32 index = 0; index < unit * run; ++index)
      *pixels.Append() = row[unit * leading + index];
  }
  u64 length = 16ull + 8ull * rows.size + pixels.size;
  if (length > UINT32_MAX)
    ScFatal(SC_ERROR_NOMEM, "Encoded image exceeds its DWORD block length");
  u8 header[16] = {};
  *size = (u32)length;
  GxImagePutDword(header, (u32)length);
  GxImagePutWord(header + 4, (u16)width);
  GxImagePutWord(header + 6, (u16)height);
  GxImagePutWord(header + 8, unit == 2 ? 4 : 3);
  GxImagePutWord(header + 10, static_cast<u16>(source->GetColorType().color));
  GxImagePutWord(header + 12, (u16)input_key);
  u8 *block = new u8[*size];
  *allocation = block;
  memcpy(block, header, sizeof(header));
  for (u32 index = 0; index < rows.size; ++index) {
    u8 *row = block + 16 + 8 * index;
    GxImagePutDword(row, rows.data[index].offset);
    GxImagePutWord(row + 4, rows.data[index].leading);
    GxImagePutWord(row + 6, rows.data[index].length);
  }
  if (pixels.size)
    memcpy(block + 16 + 8 * rows.size, pixels.data, pixels.size);
  source->Unlock(GxLockFlags::Bits);
  return true;
}

u8 GxImage::BltEdge(GxBuffer *, const ScRect &, const ScRect &, u8) {
  return 0;
}

u8 GxImage::RotateBltSimple(GxBuffer *, const ScRect &, const ScPoint &, u32, const ScRect *) {
  return 0;
}

u8 GxImage::Duplicate(GxBuffer **, GxVramRequest) {
  return false;
}

bool GxImage::IsZBuffered() const {
  return false;
}

u8 GxImage::BltNoClip(GxBuffer *, const ScRect &, const ScRect &) {
  return 0;
}

void GxImage::CopyBuffer(GxBuffer *) {
}

void GxImage::CopyZBuffer(GxBuffer *) {
}

u8 GxImage::ZBlt(GxBuffer *, u32) {
  return 0;
}

u8 GxImage::AlphaBlt(GxBuffer *, const ScRect &, const ScPoint &, f32, const ScRect *) {
  return 0;
}

// Rebuild an unscaled destination extent from the source before clipping and calling the image opacity core.
u8 GxImage::SpanBufferAlphaBltConstAlpha(
    GxBuffer *destination, f32 opacity, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferAlphaBltConstAlphaNoClip(destination, opacity, sr, output_position);
}

// Blend cropped WORD rows against their current destination using the truncated thirty-one-step opacity.
// Capture the image block before callbacks and the low WORD key afterward, matching the original observation order.
u8 GxImage::SpanBufferAlphaBltConstAlphaNoClip(
    GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScPoint *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  u16 transparent = (u16)key;
  i32 amount = (i32)(i64)((f64)opacity * 31.0), height = (i32)((u32)sr->bottom - (u32)sr->top);
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)sr->top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x, (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x), amount));
    }
  }
  return 1;
}

// Rebuild the source-sized destination extent before clipping and forwarding to the per-pixel opacity core.
u8 GxImage::SpanBufferAlphaBltSelfAlpha(
    GxBuffer *destination, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferAlphaBltSelfAlphaNoClip(destination, sr, output_position);
}

// Blend cropped WORD rows using each source pixel's low five bits as its own opacity amount.
// Capture the image block before callbacks and the low WORD key afterward, matching the original observation order.
u8 GxImage::SpanBufferAlphaBltSelfAlphaNoClip(GxBuffer *destination, const ScRect &sr_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScPoint *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  u16 transparent = (u16)key;
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)sr->top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x, (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x), pixel & 31));
    }
  }
  return 1;
}

// Rebuild the source-sized destination extent before clipping and forwarding the independent BYTE mask to the core.
u8 GxImage::SpanBufferAlphaBlt(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
    const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferAlphaBltNoClip(destination, mask, sr, output_position);
}

// Lock the BYTE mask before capturing compressed rows, then use each mask value directly as a blend amount.
u8 GxImage::SpanBufferAlphaBltNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScPoint *dr = &dr_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  u32 mask_stride = mask->GetColorSurfaceStride(), top = (u32)sr->top;
  const u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  const u8 *alpha = mask_data + (i32)((u32)sr->left + top * mask_stride);
  u16 transparent = (u16)key;
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x,
            (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x),
                alpha[(usize)y * mask_stride + (advance ? 0 : leading - (u32)sr->left) + x]));
    }
  }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Preserve the low WORD of the original biased-double conversion, including nearest-even half-way behavior.
static u16 GxImageMaskAmount(u8 value, f32 opacity) {
  f64 rounded = (f64)value * opacity + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return (u16)bits;
}

// Rebuild the source-sized destination extent before clipping and forwarding the BYTE mask and opacity to the core.
u8 GxImage::SpanBufferAlphaBltWithAdditionalAlphaFactor(GxBuffer *destination, GxBuffer *mask, f32 opacity,
    const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferAlphaBltWithAdditionalAlphaFactorNoClip(destination, mask, opacity, sr, output_position);
}

// Lock the BYTE mask before capturing rows, then round each mask value times opacity through the source double bias.
u8 GxImage::SpanBufferAlphaBltWithAdditionalAlphaFactorNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScPoint *dr = &dr_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  u32 mask_stride = mask->GetColorSurfaceStride(), top = (u32)sr->top;
  const u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  const u8 *alpha = mask_data + (i32)((u32)sr->left + top * mask_stride);
  u16 transparent = (u16)key;
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x,
            (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x),
                GxImageMaskAmount(
                    alpha[(usize)y * mask_stride + (advance ? 0 : leading - (u32)sr->left) + x], opacity)));
    }
  }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Scale-clip local rectangles, trim odd destination spans, and forward the unchanged opacity to the image core.
u8 GxImage::SpanBufferAlphaBltConstAlphaStretch(
    GxBuffer *destination, f32 opacity, const ScRect &source_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, dr = *target;
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (((u32)dr.right - (u32)dr.left) & 1)
    dr.right = (i32)((u32)dr.right - 1);
  if (((u32)dr.bottom - (u32)dr.top) & 1)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  return SpanBufferAlphaBltConstAlphaStretchNoClip(destination, opacity, sr, dr);
}

// Expand twofold or fourfold spans, preserving the original fourfold staggered rows and overlapping pixel advances.
// Each destination WORD is blended independently; wrappers own odd-edge trims and unsupported ratios return one.
u8 GxImage::SpanBufferAlphaBltConstAlphaStretchNoClip(
    GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScRect &dr_input) {
  const ScRect *sr = &sr_input;
  const ScRect *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 stride = destination->GetColorSurfaceStride() >> 1, width = (u32)sr->right - (u32)sr->left;
  i32 top = dr->top, left = dr->left;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)top * stride + left);
  i32 height = (i32)((u32)dr->bottom - (u32)top);
  u16 transparent = (u16)key;
  u32 destination_width = (u32)dr->right - (u32)left;
  i32 amount = (i32)(i64)((f64)opacity * 31.0);
  u32 scale = destination_width == 2u * width ? 2 : destination_width == 4u * width ? 4 : 0;
  if (!scale || !stride)
    return 1;
  for (i32 y = 0; y < height; y += (i32)scale) {
    const u8 *row = block + 16 + 8 * ((iptr)sr->top + y / scale);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + scale * (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent) {
        for (u32 row_index = 0; row_index < scale; ++row_index)
          for (u32 column = 0; column < scale; ++column) {
            u8 *word = target + 2 * ((usize)row_index * (stride - (scale == 4)) + column);
            GxImagePutWord(word, (u16)GxBlendColor(pixel, GxImageWord(word), amount));
          }
        target += scale == 4 ? 2 : 4;
      } else
        target += 2 * scale;
    }
  }
  return 1;
}

// Rebuild the unscaled target extent, clip it, and forward the complete packed tint color to the virtual core.
u8 GxImage::SpanBufferHighlightBlt(
    GxBuffer *destination, u32 tint, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferHighlightBltNoClip(destination, tint, sr, output_position);
}

// Blend each visible image WORD with the supplied packed color at amount fifteen, without reading destination pixels.
u8 GxImage::SpanBufferHighlightBltNoClip(
    GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScPoint *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  u16 transparent = (u16)key;
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)sr->top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x, (u16)GxBlendColor(pixel, tint, 15));
    }
  }
  return 1;
}

// Scale-clip local rectangles and trim their odd spans before entering the packed-color raster operation.
u8 GxImage::SpanBufferHighlightBltStretch(
    GxBuffer *destination, u32 tint, const ScRect &source_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, dr = *target;
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (((u32)dr.right - (u32)dr.left) & 1)
    dr.right = (i32)((u32)dr.right - 1);
  if (((u32)dr.bottom - (u32)dr.top) & 1)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  return SpanBufferHighlightBltStretchNoClip(destination, tint, sr, dr);
}

// Compute each visible tinted WORD once and duplicate it over a twofold or fourfold destination block.
// Duplicate rows retain the original DWORD-rounded pitch; unsupported horizontal ratios complete without writes.
u8 GxImage::SpanBufferHighlightBltStretchNoClip(
    GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScRect &dr_input) {
  const ScRect *sr = &sr_input;
  const ScRect *dr = &dr_input;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 stride = destination->GetColorSurfaceStride() >> 1, width = (u32)sr->right - (u32)sr->left;
  i32 top = dr->top, left = dr->left;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)top * stride + left);
  i32 height = (i32)((u32)dr->bottom - (u32)top);
  u32 destination_width = (u32)dr->right - (u32)left;
  u16 transparent = (u16)key;
  u32 scale = destination_width == 2u * width ? 2 : destination_width == 4u * width ? 4 : 0;
  if (!scale || !stride)
    return 1;
  u32 inner_pitch = 4 * (stride >> 1);
  for (i32 y = 0; y < height; y += (i32)scale) {
    const u8 *row = block + 16 + 8 * ((iptr)sr->top + y / scale);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + scale * (advance ? 0 : leading - (u32)sr->left));
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent) {
        pixel = (u16)GxBlendColor(pixel, tint, 15);
        for (u32 row_index = 0; row_index < scale; ++row_index)
          for (u32 column = 0; column < scale; ++column)
            GxImagePutWord(target + (usize)row_index * inner_pitch + 2 * (scale * x + column), pixel);
      }
    }
  }
  return 1;
}

u8 GxImage::RefreshSurfaceRestoreImage() {
  return 0;
}

void GxImage::SetSurfaceRestoreType(GxRestoreType) {
}

void GxImage::SetSurfaceRestoreFunction(GxRestoreCallback, void *, void *) {
}

u8 GxImage::Restore() {
  return 0;
}

u8 GxImage::Fill(u32, ScRect *) {
  return 0;
}

u32 GxImage::GetBitsPerPixel() const {
  return 16;
}

u8 GxImage::ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) {
  return GxGetGraphics()->ConvertRGBValueToNative16Bit(out, red, green, blue);
}

static void GxImageBlendMaskWord(u8 *target, const u8 *input, const u8 *alpha) {
  GxImagePutWord(target, (u16)GxBlendColor(GxImageWord(input), GxImageWord(target), *alpha));
}

// Scale-clip local rectangles, retaining the special twofold odd-height restoration inside the admitted clip.
u8 GxImage::SpanBufferAlphaBltStretch(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, dr = *target;
  bool twofold = 2u * ((u32)sr.right - (u32)sr.left) == (u32)dr.right - (u32)dr.left;
  bool restore = twofold && 2u * ((u32)sr.bottom - (u32)sr.top) != (u32)dr.bottom - (u32)dr.top;
  i32 bottom = dr.bottom;
  if (restore)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (restore && (u32)bottom == (u32)dr.bottom + 1 && (!clip || (bottom >= clip->top && bottom <= clip->bottom)))
    dr.bottom = bottom;
  return SpanBufferAlphaBltStretchNoClip(destination, mask, sr, &dr);
}

// Trim caller odd edges after a successful mask lock, then duplicate twofold/fourfold cropped rows.
// Twofold odd tails reuse the last image pixel with the next mask BYTE; fourfold retains staggered overlapping stores.
u8 GxImage::SpanBufferAlphaBltStretchNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, ScRect *dr) {
  const ScRect *sr = &sr_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  bool odd_width = ((u32)dr->right - (u32)dr->left) & 1;
  bool odd_height = ((u32)dr->bottom - (u32)dr->top) & 1;
  if (odd_width)
    dr->right = (i32)((u32)dr->right - 1);
  if (odd_height)
    dr->bottom = (i32)((u32)dr->bottom - 1);
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  i32 target_top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)target_top * stride + dr->left);
  i32 height = (i32)((u32)dr->bottom - (u32)target_top);
  u32 mask_stride = mask->GetColorSurfaceStride(), top = (u32)sr->top;
  u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  const u8 *alpha = mask_data + (i32)((u32)sr->left + mask_stride * top);
  u16 transparent = (u16)key;
  u32 destination_width = (u32)dr->right - (u32)dr->left;
  u32 scale = destination_width == 2u * width ? 2 : destination_width == 4u * width ? 4 : 0;
  if (scale && stride)
    for (i32 y = 0; y < height; y += (i32)scale) {
      const u8 *row = block + 16 + 8 * ((iptr)top + y / scale);
      u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
      if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
        continue;
      u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
      u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
      if (count > length - advance)
        count = length - advance;
      u32 leading_gap = advance ? 0 : leading - (u32)sr->left;
      const u8 *input = payload + 2 * (GxImageDword(row) + advance);
      const u8 *amount = alpha + (usize)(y / scale) * mask_stride + leading_gap;
      u8 *target = output + 2 * ((usize)y * stride + scale * leading_gap);
      for (u32 x = 0; x < count; ++x) {
        if ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * x) != transparent) {
          for (u32 row_index = 0; row_index < scale; ++row_index)
            for (u32 column = 0; column < scale; ++column)
              GxImageBlendMaskWord(
                  target + 2 * ((usize)row_index * (stride - (scale == 4)) + column), input + 2 * x, amount + x);
          target += scale == 4 ? 2 : 4;
        } else
          target += 2 * scale;
      }
      if (scale == 2 && odd_width &&
          ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * count - 2) != transparent)) {
        GxImageBlendMaskWord(target, input + 2 * count - 2, amount + count);
        GxImageBlendMaskWord(target + 2 * stride, input + 2 * count - 2, amount + count);
      }
      if (scale == 2 && odd_height && y + 2 >= height) {
        target = output + 2 * (usize)height * stride;
        for (u32 x = 0; x < count; ++x) {
          if (GxImageWord(input + 2 * x) != transparent) {
            GxImageBlendMaskWord(target, input + 2 * x, amount + x);
            GxImageBlendMaskWord(target + 2, input + 2 * x, amount + x);
          }
          target += 4;
        }
        if (odd_width)
          GxImageBlendMaskWord(target, input + 2 * count - 2, amount + count);
      }
    }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Retain the signed full-DWORD result of the scaled mask conversion rather than the unscaled low-WORD form.
static i32 GxImageScaledMaskAmount(u8 value, f32 opacity) {
  f64 rounded = (f64)value * opacity + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return (i32)((u32)bits + kScDoubleIntegerLowWordBias);
}

static void GxImageBlendMaskWord(u8 *target, const u8 *input, i32 amount) {
  GxImagePutWord(target, (u16)GxBlendColor(GxImageWord(input), GxImageWord(target), amount));
}

// Scale-clip local rectangles and forward the unchanged mask and opacity without the separate plain-mask height rule.
u8 GxImage::SpanBufferAlphaBltWithAdditionalAlphaFactorStretch(GxBuffer *destination, GxBuffer *mask, f32 opacity,
    const ScRect &source_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, dr = *target;
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  return SpanBufferAlphaBltWithAdditionalAlphaFactorStretchNoClip(destination, mask, opacity, sr, &dr);
}

// Trim odd caller edges after locking the mask, then duplicate each pixel using its rounded full-DWORD amount.
// Twofold odd columns reuse the last amount; the extra row always key-tests and retains its own last admitted amount.
u8 GxImage::SpanBufferAlphaBltWithAdditionalAlphaFactorStretchNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, ScRect *dr) {
  const ScRect *sr = &sr_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  bool odd_width = ((u32)dr->right - (u32)dr->left) & 1;
  bool odd_height = ((u32)dr->bottom - (u32)dr->top) & 1;
  if (odd_width)
    dr->right = (i32)((u32)dr->right - 1);
  if (odd_height)
    dr->bottom = (i32)((u32)dr->bottom - 1);
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  i32 target_top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)target_top * stride + dr->left);
  i32 height = (i32)((u32)dr->bottom - (u32)target_top);
  u32 mask_stride = mask->GetColorSurfaceStride(), top = (u32)sr->top;
  u32 mask_left = (u32)sr->left;
  u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  const u8 *alpha = mask_data + (i32)(mask_left + mask_stride * top);
  u16 transparent = (u16)key;
  u32 destination_width = (u32)dr->right - (u32)dr->left;
  u32 scale_width = (u32)sr->right - mask_left;
  u32 scale = destination_width == 2u * scale_width ? 2 : destination_width == 4u * scale_width ? 4 : 0;
  i32 retained_amount = (i32)mask_left;
  if (scale && stride)
    for (i32 y = 0; y < height; y += (i32)scale) {
      const u8 *row = block + 16 + 8 * ((iptr)top + y / scale);
      u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
      if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
        continue;
      u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
      u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
      if (count > length - advance)
        count = length - advance;
      u32 leading_gap = advance ? 0 : leading - (u32)sr->left;
      const u8 *input = payload + 2 * (GxImageDword(row) + advance);
      const u8 *amount = alpha + (usize)(y / scale) * mask_stride + leading_gap;
      u8 *target = output + 2 * ((usize)y * stride + scale * leading_gap);
      for (u32 x = 0; x < count; ++x) {
        if ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * x) != transparent) {
          retained_amount = GxImageScaledMaskAmount(amount[x], opacity);
          for (u32 row_index = 0; row_index < scale; ++row_index)
            for (u32 column = 0; column < scale; ++column)
              GxImageBlendMaskWord(
                  target + 2 * ((usize)row_index * (stride - (scale == 4)) + column), input + 2 * x, retained_amount);
          target += scale == 4 ? 2 : 4;
        } else
          target += 2 * scale;
      }
      if (scale == 2 && odd_width &&
          ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * count - 2) != transparent)) {
        GxImageBlendMaskWord(target, input + 2 * count - 2, retained_amount);
        GxImageBlendMaskWord(target + 2 * stride, input + 2 * count - 2, retained_amount);
      }
      if (scale == 2 && odd_height && y + 2 >= height) {
        target = output + 2 * (usize)height * stride;
        for (u32 x = 0; x < count; ++x) {
          if (GxImageWord(input + 2 * x) != transparent) {
            retained_amount = GxImageScaledMaskAmount(amount[x], opacity);
            GxImageBlendMaskWord(target, input + 2 * x, retained_amount);
            GxImageBlendMaskWord(target + 2, input + 2 * x, retained_amount);
          }
          target += 4;
        }
        if (odd_width && GxImageWord(input + 2 * count - 2) != transparent)
          GxImageBlendMaskWord(target, input + 2 * count - 2, retained_amount);
      }
    }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Clip a source-sized target before copying and adjusting the independently positioned mask rectangle.
u8 GxImage::SpanBufferAlphaBltWithSeparateAlphaRect(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
    const ScRect &mask_area_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *mask_area = &mask_area_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  ScRect mr = *mask_area;
  mr.left = (i32)((u32)mr.left + (u32)sr.left - (u32)source->left);
  mr.right = (i32)((u32)mr.right + (u32)sr.right - (u32)source->right);
  mr.top = (i32)((u32)mr.top + (u32)sr.top - (u32)source->top);
  mr.bottom = (i32)((u32)mr.bottom + (u32)sr.bottom - (u32)source->bottom);
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferAlphaBltWithSeparateAlphaRectNoClip(destination, mask, sr, mr, output_position);
}

// Blend cropped image WORDs with a separately positioned BYTE mask, retaining the original leading-edge address rule.
// Capture source row position after both destination and mask getters; only the mask is locked and released.
u8 GxImage::SpanBufferAlphaBltWithSeparateAlphaRectNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input,
    const ScRect &mask_area_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScRect *mask_area = &mask_area_input;
  const ScPoint *dr = &dr_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  u32 mask_stride = mask->GetColorSurfaceStride();
  const u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  u32 mask_origin = (u32)mask_area->left + (u32)mask_area->top * mask_stride;
  u16 transparent = (u16)key;
  u32 top = (u32)sr->top;
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    u32 mask_gap = leading <= (u32)sr->left ? 0 : leading - (u32)mask_area->left;
    const u8 *alpha = mask_data + (i32)(mask_origin + (u32)y * mask_stride + mask_gap);
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(target + 2 * x, (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x), alpha[x]));
    }
  }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Copy both source rectangles before clipping a source-sized target, then adjust the independent second-mask edges.
u8 GxImage::SpanBufferBltWith2Alphas(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
    const ScRect &source_input, const ScRect &second_area_input, const ScPoint &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *second_area = &second_area_input;
  const ScPoint *target = &target_input;
  ScRect sr = *source, mr = *second_area;
  ScRect dr = {target->x, target->y, (i32)((u32)target->x + (u32)sr.right - (u32)sr.left),
      (i32)((u32)target->y + (u32)sr.bottom - (u32)sr.top)};
  if (!destination->GenericBufferClip(destination, &sr, &dr, clip))
    return 0;
  mr.left = (i32)((u32)mr.left + (u32)sr.left - (u32)source->left);
  mr.right = (i32)((u32)mr.right + (u32)sr.right - (u32)source->right);
  mr.top = (i32)((u32)mr.top + (u32)sr.top - (u32)source->top);
  mr.bottom = (i32)((u32)mr.bottom + (u32)sr.bottom - (u32)source->bottom);
  const ScPoint output_position{dr.left, dr.top};
  return SpanBufferBltWith2AlphasNoClip(destination, first_mask, second_mask, sr, mr, output_position);
}

// Lock the two BYTE masks in order, retaining the first lock if the second fails, then blend their product shifted by five.
// Read each mask origin after its own data getter and release the second mask before the first.
u8 GxImage::SpanBufferBltWith2AlphasNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
    const ScRect &sr_input, const ScRect &second_area_input, const ScPoint &dr_input) {
  const ScRect *sr = &sr_input;
  const ScRect *second_area = &second_area_input;
  const ScPoint *dr = &dr_input;
  if (!first_mask->Lock(GxLockFlags::Bits) || !second_mask->Lock(GxLockFlags::Bits))
    return 0;
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)dr->y * stride + dr->x);
  i32 height = (i32)((u32)sr->bottom - (u32)sr->top);
  u32 first_stride = first_mask->GetColorSurfaceStride();
  const u8 *first_data = static_cast<u8 *>(first_mask->GetColorSurfaceBits());
  u32 first_origin = (u32)sr->left + (u32)sr->top * first_stride;
  u32 second_stride = second_mask->GetColorSurfaceStride();
  const u8 *second_data = static_cast<u8 *>(second_mask->GetColorSurfaceBits());
  u32 second_origin = (u32)second_area->left + (u32)second_area->top * second_stride;
  u16 transparent = (u16)key;
  u32 top = (u32)sr->top;
  for (i32 y = 0; stride && y < height; ++y) {
    const u8 *row = block + 16 + 8 * ((iptr)top + y);
    u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
    if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
      continue;
    u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
    u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
    if (count > length - advance)
      count = length - advance;
    const u8 *input = payload + 2 * (GxImageDword(row) + advance);
    u8 *target = output + 2 * ((usize)y * stride + (advance ? 0 : leading - (u32)sr->left));
    u32 gap = advance ? 0 : leading - (u32)sr->left;
    const u8 *first = first_data + (i32)(first_origin + (u32)y * first_stride + gap);
    const u8 *second = second_data + (i32)(second_origin + (u32)y * second_stride + gap);
    for (u32 x = 0; x < count; ++x) {
      u16 pixel = GxImageWord(input + 2 * x);
      if ((record & kGxImageRowOpaque) || pixel != transparent)
        GxImagePutWord(
            target + 2 * x, (u16)GxBlendColor(pixel, GxImageWord(target + 2 * x), (u32)first[x] * second[x] >> 5));
    }
  }
  second_mask->Unlock(GxLockFlags::Bits);
  first_mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Scale-clip with the original twofold height restoration, then copy and adjust the live independent mask rectangle.
u8 GxImage::SpanBufferAlphaBltWithSeparateAlphaRectStretch(GxBuffer *destination, GxBuffer *mask,
    const ScRect &source_input, const ScRect &mask_area_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *mask_area = &mask_area_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, dr = *target;
  bool twofold = 2u * ((u32)sr.right - (u32)sr.left) == (u32)dr.right - (u32)dr.left;
  bool restore = twofold && 2u * ((u32)sr.bottom - (u32)sr.top) != (u32)dr.bottom - (u32)dr.top;
  i32 bottom = dr.bottom;
  if (restore)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  if (restore && (u32)bottom == (u32)dr.bottom + 1 && (!clip || (bottom >= clip->top && bottom <= clip->bottom)))
    dr.bottom = bottom;
  ScRect mr = *mask_area;
  mr.left = (i32)((u32)mr.left + (u32)sr.left - (u32)source->left);
  mr.right = (i32)((u32)mr.right + (u32)sr.right - (u32)source->right);
  mr.top = (i32)((u32)mr.top + (u32)sr.top - (u32)source->top);
  mr.bottom = (i32)((u32)mr.bottom + (u32)sr.bottom - (u32)source->bottom);
  return SpanBufferAlphaBltWithSeparateAlphaRectStretchNoClip(destination, mask, sr, mr, &dr);
}

// Trim odd target edges after locking the independently positioned mask, then duplicate twofold/fourfold rows.
// Twofold odd tails reuse the last image pixel with the next mask BYTE; fourfold retains staggered overlapping stores.
u8 GxImage::SpanBufferAlphaBltWithSeparateAlphaRectStretchNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, const ScRect &mask_area_input, ScRect *dr) {
  const ScRect *sr = &sr_input;
  const ScRect *mask_area = &mask_area_input;
  if (!mask->Lock(GxLockFlags::Bits))
    return 0;
  bool odd_width = ((u32)dr->right - (u32)dr->left) & 1;
  bool odd_height = ((u32)dr->bottom - (u32)dr->top) & 1;
  if (odd_width)
    dr->right = (i32)((u32)dr->right - 1);
  if (odd_height)
    dr->bottom = (i32)((u32)dr->bottom - 1);
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  i32 target_top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)target_top * stride + dr->left);
  i32 height = (i32)((u32)dr->bottom - (u32)target_top);
  u32 mask_stride = mask->GetColorSurfaceStride();
  const u8 *mask_data = static_cast<u8 *>(mask->GetColorSurfaceBits());
  u32 mask_origin = (u32)mask_area->left + (u32)mask_area->top * mask_stride;
  u16 transparent = (u16)key;
  u32 top = (u32)sr->top;
  u32 destination_width = (u32)dr->right - (u32)dr->left;
  u32 scale = destination_width == 2u * width ? 2 : destination_width == 4u * width ? 4 : 0;
  if (scale && stride)
    for (i32 y = 0; y < height; y += (i32)scale) {
      const u8 *row = block + 16 + 8 * ((iptr)top + y / scale);
      u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
      if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
        continue;
      u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
      u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
      if (count > length - advance)
        count = length - advance;
      u32 leading_gap = advance ? 0 : leading - (u32)sr->left;
      const u8 *input = payload + 2 * (GxImageDword(row) + advance);
      u32 mask_gap = leading <= (u32)sr->left ? 0 : leading - (u32)mask_area->left;
      const u8 *amount = mask_data + (i32)(mask_origin + (u32)(y / scale) * mask_stride + mask_gap);
      u8 *target = output + 2 * ((usize)y * stride + scale * leading_gap);
      for (u32 x = 0; x < count; ++x) {
        if ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * x) != transparent) {
          for (u32 row_index = 0; row_index < scale; ++row_index)
            for (u32 column = 0; column < scale; ++column)
              GxImageBlendMaskWord(
                  target + 2 * ((usize)row_index * (stride - (scale == 4)) + column), input + 2 * x, amount + x);
          target += scale == 4 ? 2 : 4;
        } else
          target += 2 * scale;
      }
      if (scale == 2 && odd_width &&
          ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * count - 2) != transparent)) {
        GxImageBlendMaskWord(target, input + 2 * count - 2, amount + count);
        GxImageBlendMaskWord(target + 2 * stride, input + 2 * count - 2, amount + count);
      }
      if (scale == 2 && odd_height && y + 2 >= height) {
        target = output + 2 * (usize)height * stride;
        for (u32 x = 0; x < count; ++x) {
          if (GxImageWord(input + 2 * x) != transparent) {
            GxImageBlendMaskWord(target, input + 2 * x, amount + x);
            GxImageBlendMaskWord(target + 2, input + 2 * x, amount + x);
          }
          target += 4;
        }
        if (odd_width)
          GxImageBlendMaskWord(target, input + 2 * count - 2, amount + count);
      }
    }
  mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Copy the source and independent mask area before scale clipping, preserving height restoration for every ratio.
u8 GxImage::SpanBufferBltWith2AlphasStretch(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
    const ScRect &source_input, const ScRect &second_area_input, const ScRect &target_input, const ScRect *clip) {
  const ScRect *source = &source_input;
  const ScRect *second_area = &second_area_input;
  const ScRect *target = &target_input;
  ScRect sr = *source, mr = *second_area, dr = *target;
  bool restore = 2u * ((u32)sr.bottom - (u32)sr.top) != (u32)dr.bottom - (u32)dr.top;
  i32 bottom = dr.bottom;
  if (restore)
    dr.bottom = (i32)((u32)dr.bottom - 1);
  if (!destination->GenericBufferClipStretch(destination, &sr, &dr, clip, nullptr, nullptr))
    return 0;
  mr.left = (i32)((u32)mr.left + (u32)sr.left - (u32)source->left);
  mr.right = (i32)((u32)mr.right + (u32)sr.right - (u32)source->right);
  mr.top = (i32)((u32)mr.top + (u32)sr.top - (u32)source->top);
  mr.bottom = (i32)((u32)mr.bottom + (u32)sr.bottom - (u32)source->bottom);
  if (restore && (u32)bottom == (u32)dr.bottom + 1 && (!clip || (bottom >= clip->top && bottom <= clip->bottom)))
    dr.bottom = bottom;
  return SpanBufferBltWith2AlphasStretchNoClip(destination, first_mask, second_mask, sr, mr, &dr);
}

// The original passes the source rectangle's address as a blend amount before any pixel sample.
// Valid original user addresses cannot index the initialized blend table, even after DWORD addition.
static i32 GxImageInitialMaskAmount(const ScRect *source) {
  uptr address = reinterpret_cast<uptr>(source);
  assert(address < sizeof(GxBlend));
  return static_cast<i32>(static_cast<u32>(address));
}

// Lock both masks before trimming odd target edges and sample one full product for each duplicated source pixel.
// Odd columns retain the last amount; before the first sample the original uses the source rectangle's address.
u8 GxImage::SpanBufferBltWith2AlphasStretchNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
    const ScRect &sr_input, const ScRect &second_area_input, ScRect *dr) {
  const ScRect *sr = &sr_input;
  const ScRect *second_area = &second_area_input;
  if (!first_mask->Lock(GxLockFlags::Bits) || !second_mask->Lock(GxLockFlags::Bits))
    return 0;
  bool odd_width = ((u32)dr->right - (u32)dr->left) & 1;
  bool odd_height = ((u32)dr->bottom - (u32)dr->top) & 1;
  if (odd_width)
    dr->right = (i32)((u32)dr->right - 1);
  if (odd_height)
    dr->bottom = (i32)((u32)dr->bottom - 1);
  const u8 *block = data, *payload = block + 16 + 8 * (u32)GxImageWord(block + 6);
  u32 width = (u32)sr->right - (u32)sr->left, stride = destination->GetColorSurfaceStride() >> 1;
  i32 target_top = dr->top;
  u8 *output = static_cast<u8 *>(destination->GetColorSurfaceBits());
  output += 2 * ((iptr)target_top * stride + dr->left);
  i32 height = (i32)((u32)dr->bottom - (u32)target_top);
  u32 first_stride = first_mask->GetColorSurfaceStride(), second_stride = second_mask->GetColorSurfaceStride();
  const u8 *first_data = static_cast<u8 *>(first_mask->GetColorSurfaceBits());
  u32 first_origin = (u32)sr->left + (u32)sr->top * first_stride;
  const u8 *second_data = static_cast<u8 *>(second_mask->GetColorSurfaceBits());
  u32 second_origin = (u32)second_area->left + (u32)second_area->top * second_stride;
  u16 transparent = (u16)key;
  u32 top = (u32)sr->top;
  u32 destination_width = (u32)dr->right - (u32)dr->left;
  u32 scale_width = (u32)sr->right - (u32)sr->left;
  u32 scale = destination_width == 2u * scale_width ? 2 : destination_width == 4u * scale_width ? 4 : 0;
  i32 retained_amount = 0;
  bool amount_ready = false;
  if (scale && stride)
    for (i32 y = 0; y < height; y += (i32)scale) {
      const u8 *row = block + 16 + 8 * ((iptr)top + y / scale);
      u32 leading = GxImageWord(row + 4), record = GxImageWord(row + 6), length = record & kGxImageRowLengthMask;
      if (sr->left >= (i32)(leading + length) || sr->right <= (i32)leading)
        continue;
      u32 advance = leading <= (u32)sr->left ? (u32)sr->left - leading : 0;
      u32 count = leading <= (u32)sr->left ? width : (u32)sr->right - leading;
      if (count > length - advance)
        count = length - advance;
      u32 leading_gap = advance ? 0 : leading - (u32)sr->left;
      const u8 *input = payload + 2 * (GxImageDword(row) + advance);
      const u8 *first = first_data + (i32)(first_origin + (u32)(y / scale) * first_stride + leading_gap);
      const u8 *second = second_data + (i32)(second_origin + (u32)(y / scale) * second_stride + leading_gap);
      u8 *target = output + 2 * ((usize)y * stride + scale * leading_gap);
      for (u32 x = 0; x < count; ++x) {
        if ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * x) != transparent) {
          retained_amount = (i32)((u32)first[x] * second[x] >> 5);
          amount_ready = true;
          for (u32 row_index = 0; row_index < scale; ++row_index)
            for (u32 column = 0; column < scale; ++column)
              GxImageBlendMaskWord(
                  target + 2 * ((usize)row_index * (stride - (scale == 4)) + column), input + 2 * x, retained_amount);
          target += scale == 4 ? 2 : 4;
        } else
          target += 2 * scale;
      }
      if (scale == 2 && odd_width &&
          ((record & kGxImageRowOpaque) || GxImageWord(input + 2 * count - 2) != transparent)) {
        if (!amount_ready)
          retained_amount = GxImageInitialMaskAmount(sr);
        GxImageBlendMaskWord(target, input + 2 * count - 2, retained_amount);
        GxImageBlendMaskWord(target + 2 * stride, input + 2 * count - 2, retained_amount);
      }
      if (scale == 2 && odd_height && y + 2 >= height) {
        target = output + 2 * (usize)height * stride;
        for (u32 x = 0; x < count; ++x) {
          if (GxImageWord(input + 2 * x) != transparent) {
            retained_amount = (i32)((u32)first[x] * second[x] >> 5);
            amount_ready = true;
            GxImageBlendMaskWord(target, input + 2 * x, retained_amount);
            GxImageBlendMaskWord(target + 2, input + 2 * x, retained_amount);
          }
          target += 4;
        }
        if (odd_width && GxImageWord(input + 2 * count - 2) != transparent) {
          if (!amount_ready)
            retained_amount = GxImageInitialMaskAmount(sr);
          GxImageBlendMaskWord(target, input + 2 * count - 2, retained_amount);
        }
      }
    }
  second_mask->Unlock(GxLockFlags::Bits);
  first_mask->Unlock(GxLockFlags::Bits);
  return 1;
}

// Fill and draw through a temporary borrowed image, then convert the complete destination only after unlocking it.
// Detach the borrowed block before temporary destruction; neither an explicit block nor this image's block is consumed.
u8 GxImage::ConvertDataToStandardBuffer(GxBuffer *destination, u8 *allocation) {
  GxImage temporary;
  u8 *block = allocation ? allocation : data;
  temporary.ImportFromData(block, 1);
  destination->Fill(GxImageWord(block + 12), nullptr);
  u8 result = 0;
  if (destination->Lock(GxLockFlags::Bits)) {
    ScRect source = {0, 0, GxImageWord(block + 4), GxImageWord(block + 6)}, target = source;
    result = temporary.BltSrc(destination, source, target, nullptr);
    destination->Unlock(GxLockFlags::Bits);
    if (GxImageWord(block + 8) == 4) {
      i32 code = (i16)GxImageWord(block + 10);
      if (code != static_cast<i32>(destination->GetColorType().color))
        destination->Convert16BitBufferFromFormatToFormat(
            static_cast<GxColorType::Color>((u32)code), static_cast<GxColorType::Color>(1));
    }
  }
  temporary.ImportFromData(nullptr, 1);
  return result;
}

// Publish the factory's buffer directly, then initialize from the image category and transfer its borrowed rows.
// Failed initialization retains the published object; the factory Boolean and transfer cleanup results are ignored.
u8 GxImage::ConvertDataToStandardBuffer(GxBuffer **out, u8 *allocation) {
  // Preserve the caller's actual output address across the external COM interface-output boundary.
  GxGetDirector()->GZCOM()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(out));
  GxColorType requested(static_cast<GxColorType::Color>((i16)GxImageWord(allocation + 8)));
  if (!(*out)->Init(GxImageWord(allocation + 4), GxImageWord(allocation + 6), requested, nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
    return 0;
  return ConvertDataToStandardBuffer(*out, allocation);
}

// Reject different dimensions before decoding and compare through the input buffer's actual mismatch operation.
// The source raw-frees the decoded allocation without destruction, retaining its backend leak and skipped release.
bool GxImage::ValidateStandardBufferToDataConversion(GxBuffer *buffer, u8 *allocation) {
  GxBuffer *decoded = nullptr;
  if (buffer->Width() != GxImageWord(allocation + 4) || buffer->Height() != GxImageWord(allocation + 6) ||
      !ConvertDataToStandardBuffer(&decoded, allocation))
    return false;
  bool equal = buffer->CompareToBuffer(decoded) == 0;
  ::operator delete(decoded);
  return equal;
}

bool GxImage::ValidateStandardBufferToDataConversion(GxBuffer *buffer) {
  return ValidateStandardBufferToDataConversion(buffer, data);
}

u8 GxImage::GetMemoryUsage(u32 *, u32 *) {
  return 0;
}

u8 GxImage::AlphaSourcePrecalculate(const ScRect &, f32) {
  return 0;
}

GxPalette::GxPalette() : references(0), count(0), bits(0), pixels(nullptr) {
}

// Publish and retain the original base/palette identities. Failed queries clear the output.
bool GxPalette::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_GxPaletteView)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<GxPaletteView *>(this);
  AddRef();
  return 1;
}

u32 GxPalette::AddRef() {
  return ++references;
}

// Destroy the palette when the old reference count was one. Zero decrements wrap as in the source.
u32 GxPalette::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

bool GxPalette::Init() {
  return 1;
}

bool GxPalette::Shutdown() {
  return 1;
}

i32 GxPalette::GetNumberEntries() {
  return count;
}

i32 GxPalette::GetBitsPerPixel() {
  return bits;
}

// Read a valid entry into the genuine DWORD output. Unsupported storage and invalid indices leave it unchanged.
bool GxPalette::GetColor(i32 index, u32 *out) {
  if (index < 0 || index >= count)
    return 0;
  if (bits == 16) {
    u16 pixel;
    memcpy(&pixel, pixels + 2 * (usize)index, sizeof(pixel));
    *out = pixel;
    return 1;
  }
  if (bits == 24) {
    u8 *pixel = pixels + 3 * (usize)index;
    *out = (u32)pixel[0] << 16;
    *out |= (u32)pixel[1] << 8;
    *out |= pixel[2];
    return 1;
  }
  if (bits == 32) {
    memcpy(out, pixels + 4 * (usize)index, sizeof(*out));
    return 1;
  }
  return 0;
}

// Store the source pixel at the requested depth.
bool GxPalette::SetColor(i32 index, const u32 &pixel) {
  if (index < 0 || index >= count)
    return 0;
  if (bits == 16) {
    memcpy(pixels + 2 * (usize)index, &pixel, 2);
    return 1;
  }
  if (bits == 24) {
    pixels[3 * (usize)index] = 0;
    pixels[3 * (usize)index + 1] = 0;
    pixels[3 * (usize)index + 2] = 0;
    return 1;
  }
  if (bits == 32)
    memcpy(pixels + 4 * (usize)index, &pixel, 4);
  return 0;
}

// Publish the count before replacing storage, rounding entries to even and retaining signed division and DWORD size.
bool GxPalette::MakeGenericPalette(i32 size, i32 depth) {
  count = size;
  delete[] pixels;
  i64 rounded = (((i64)size - 1) | 1) + 1;
  u32 bytes = (u32)(rounded * depth / 8);
  u8 *replacement = new u8[bytes];
  bits = depth;
  pixels = replacement;
  return 1;
}

// Return the borrowed mutable pixel allocation.
void *GxPalette::GetPaletteArray() {
  return pixels;
}

GxPalette::~GxPalette() {
  delete[] pixels;
}

// Visit every live entry through the virtual reader and return the last visited index, or zero for an empty palette.
// The original ignores the requested color and the retrieved values; preserve that behavior rather than searching.
i32 GxPalette::GetNearestIndex(const u32 &) {
  i32 result = 0;
  u32 value;
  for (i32 index = 0; index < count; ++index) {
    GetColor(index, &value);
    result = index;
  }
  return result;
}

bool GxPalette::IsSystemPalette() {
  return false;
}

bool GxPalette::IsReserved(i32) {
  return false;
}
