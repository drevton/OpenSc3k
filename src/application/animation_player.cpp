#include "application/animation_player.h"

#if defined(_WIN32)
// Windows SDK DirectX headers require the multimedia types to be defined first.
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <dsound.h>
// clang-format on
#undef GetObject
#undef PlaySound
#include <process.h>
#include <unknwn.h>
#endif
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include "graphics/sdl_platform.h"

#include "application/application_core.h"
#include "audio/device.h"
#include "audio/sample.h"
#include "common/api.h"
#include "common/path.h"
#include "common/rect.h"
#include "common/sc3_error.h"
#include "common/sc3_qfs.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"
#include "resources/file.h"

// Movie decoders retain upper halfwords, spare packed-pixel bits and stream control bits with their original masks.
enum ApVideoPackedWord : u32 {
  kApVideoUpperHalfwordMask = 0xffff0000,
  kApVideoPacked16SpareBits = 0x80008000,
  kApVideoByteAverageMask = 0x7f7f7f7f,
  kApVideoOldFrameModeMask = 0xc0000000,
  kApVideoStreamTopBit = 0x80000000,
  kApVideoChunkHeaderRewind = 0xfffffff8,
};

// YUV contributions, the TQI transform and signed three-byte values retain their original fixed-point scales and modulus.
enum ApVideoFixedScale : i32 {
  kApVideoBlueChromaQ16 = 0x1c5a1,
  kApVideoPackedLumaScale = 0x800801,
  kApVideoPackedLumaBias = 0x4883d8a9,
  kApVideoInverseSqrtTwoQ31 = 0x5a82799a,
  kApVideoThreeBytePositiveMax = 0x7fffff,
  kApVideoThreeByteModulus = 0x1000000,
};

// Reference overlays use YUY2, a neutral packed fill, wait-plus-color-fill blits and show-plus-overlay-effects flags.
enum ApVideoReferencePresentation : u32 {
  kApVideoYuy2FourCc = 0x32595559,
  kApVideoOverlayNeutralFill = 0x80808080,
  kApVideoWaitColorFillFlags = 0x1000400,
  kApVideoShowOverlayEffectsFlags = 0x84000,
};

// Movie chunk tags select audio headers/data/end markers or a particular video decoder; reversed tags are accepted aliases.
enum ApMovieChunkTag : u32 {
  kApAudioDataSndc = 0x43444e53,
  kApAudioDataSndcReversed = 0x534e4443,
  kApAudioDataScdlReversed = 0x5343446c,
  kApAudioData1Snd = 0x644e5331,
  kApAudioDataScdl = 0x6c444353,
  kApAudioHeaderSead = 0x44414553,
  kApAudioHeaderSchlReversed = 0x5343486c,
  kApAudioHeader1Snh = 0x684e5331,
  kApAudioHeaderSchl = 0x6c484353,
  kApAudioEndSend = 0x444e4553,
  kApAudioEnd1Sne = 0x654e5331,
  kApAudioEndScel = 0x6c454353,
  kApAudioAuxiliaryScclReversed = 0x5343436c,
  kApAudioAuxiliaryScllReversed = 0x53434c6c,
  kApAudioAuxiliarySccl = 0x6c434353,
  kApAudioAuxiliaryScll = 0x6c4c4353,
  kApVideoTgvKeyFrame = 0x6b564754,
  kApVideoTgvFrameReversed = 0x54475666,
  kApVideoTgvFrame = 0x66564754,
  kApVideoTgqFrameReversed = 0x54475170,
  kApVideoMuvFrame = 0x6656554d,
  kApVideoTqiFrameReversed = 0x54514970,
  kApVideoUv2Frame = 0x66325655,
  kApVideoMadKeyFrameReversed = 0x4d41446b,
  kApVideoMadKeyFrame = 0x6b44414d,
  kApVideoMadMFrameReversed = 0x4d41446d,
  kApVideoMadMFrame = 0x6d44414d,
  kApVideoMadEFrameReversed = 0x4d414465,
  kApVideoMadEFrame = 0x6544414d,
};

// The circular frame and audio queues wrap within these original byte capacities.
enum ApMovieStorageSize : i32 {
  kApVideoFrameStorageBytes = 0x124f80,
  kApVideoAudioStorageBytes = 0x493e0,
};

// Reference movie presentation preserves the error identities named by its original DirectDraw diagnostics.
enum ApVideoDrawError : u32 {
  kApVideoDDERR_ALREADYINITIALIZED = 0x88760005,
  kApVideoDDERR_CANNOTATTACHSURFACE = 0x8876000a,
  kApVideoDDERR_CANNOTDETACHSURFACE = 0x88760014,
  kApVideoDDERR_CURRENTLYNOTAVAIL = 0x88760028,
  kApVideoDDERR_EXCEPTION = 0x88760037,
  kApVideoDDERR_GENERIC = 0x80004005,
  kApVideoDDERR_HEIGHTALIGN = 0x8876005a,
  kApVideoDDERR_INCOMPATIBLEPRIMARY = 0x8876005f,
  kApVideoDDERR_INVALIDCAPS = 0x88760064,
  kApVideoDDERR_INVALIDCLIPLIST = 0x8876006e,
  kApVideoDDERR_INVALIDMODE = 0x88760078,
  kApVideoDDERR_INVALIDOBJECT = 0x88760082,
  kApVideoDDERR_INVALIDPARAMS = 0x80070057,
  kApVideoDDERR_INVALIDPIXELFORMAT = 0x88760091,
  kApVideoDDERR_INVALIDRECT = 0x88760096,
  kApVideoDDERR_LOCKEDSURFACES = 0x887600a0,
  kApVideoDDERR_NO3D = 0x887600aa,
  kApVideoDDERR_NOALPHAHW = 0x887600b4,
  kApVideoDDERR_NOCLIPLIST = 0x887600cd,
  kApVideoDDERR_NOCOLORCONVHW = 0x887600d2,
  kApVideoDDERR_NOCOOPERATIVELEVELSET = 0x887600d4,
  kApVideoDDERR_NOCOLORKEY = 0x887600d7,
  kApVideoDDERR_NOCOLORKEYHW = 0x887600dc,
  kApVideoDDERR_NODIRECTDRAWSUPPORT = 0x887600de,
  kApVideoDDERR_NOEXCLUSIVEMODE = 0x887600e1,
  kApVideoDDERR_NOFLIPHW = 0x887600e6,
  kApVideoDDERR_NOGDI = 0x887600f0,
  kApVideoDDERR_NOMIRRORHW = 0x887600fa,
  kApVideoDDERR_NOTFOUND = 0x887600ff,
  kApVideoDDERR_NOOVERLAYHW = 0x88760104,
  kApVideoDDERR_NORASTEROPHW = 0x88760118,
  kApVideoDDERR_NOROTATIONHW = 0x88760122,
  kApVideoDDERR_NOSTRETCHHW = 0x88760136,
  kApVideoDDERR_NOT4BITCOLOR = 0x8876013c,
  kApVideoDDERR_NOT4BITCOLORINDEX = 0x8876013d,
  kApVideoDDERR_NOT8BITCOLOR = 0x88760140,
  kApVideoDDERR_NOTEXTUREHW = 0x8876014a,
  kApVideoDDERR_NOVSYNCHW = 0x8876014f,
  kApVideoDDERR_NOZBUFFERHW = 0x88760154,
  kApVideoDDERR_NOZOVERLAYHW = 0x8876015e,
  kApVideoDDERR_OUTOFCAPS = 0x88760168,
  kApVideoDDERR_OUTOFMEMORY = 0x8007000e,
  kApVideoDDERR_OUTOFVIDEOMEMORY = 0x8876017c,
  kApVideoDDERR_OVERLAYCANTCLIP = 0x8876017e,
  kApVideoDDERR_OVERLAYCOLORKEYONLYONEACTIVE = 0x88760180,
  kApVideoDDERR_PALETTEBUSY = 0x88760183,
  kApVideoDDERR_COLORKEYNOTSET = 0x88760190,
  kApVideoDDERR_SURFACEALREADYATTACHED = 0x8876019a,
  kApVideoDDERR_SURFACEALREADYDEPENDENT = 0x887601a4,
  kApVideoDDERR_SURFACEBUSY = 0x887601ae,
  kApVideoDDERR_SURFACEISOBSCURED = 0x887601b8,
  kApVideoDDERR_SURFACELOST = 0x887601c2,
  kApVideoDDERR_SURFACENOTATTACHED = 0x887601cc,
  kApVideoDDERR_TOOBIGHEIGHT = 0x887601d6,
  kApVideoDDERR_TOOBIGSIZE = 0x887601e0,
  kApVideoDDERR_TOOBIGWIDTH = 0x887601ea,
  kApVideoDDERR_UNSUPPORTED = 0x80004001,
  kApVideoDDERR_UNSUPPORTEDFORMAT = 0x887601fe,
  kApVideoDDERR_UNSUPPORTEDMASK = 0x88760208,
  kApVideoDDERR_VERTICALBLANKINPROGRESS = 0x88760219,
  kApVideoDDERR_WASSTILLDRAWING = 0x8876021c,
  kApVideoDDERR_XALIGN = 0x88760230,
  kApVideoDDERR_INVALIDDIRECTDRAWGUID = 0x88760231,
  kApVideoDDERR_DIRECTDRAWALREADYCREATED = 0x88760232,
  kApVideoDDERR_NODIRECTDRAWHW = 0x88760233,
  kApVideoDDERR_PRIMARYSURFACEALREADYEXISTS = 0x88760234,
  kApVideoDDERR_NOEMULATION = 0x88760235,
  kApVideoDDERR_REGIONTOOSMALL = 0x88760236,
  kApVideoDDERR_CLIPPERISUSINGHWND = 0x88760237,
  kApVideoDDERR_NOCLIPPERATTACHED = 0x88760238,
  kApVideoDDERR_NOHWND = 0x88760239,
  kApVideoDDERR_HWNDSUBCLASSED = 0x8876023a,
  kApVideoDDERR_HWNDALREADYSET = 0x8876023b,
  kApVideoDDERR_NOPALETTEATTACHED = 0x8876023c,
  kApVideoDDERR_NOPALETTEHW = 0x8876023d,
  kApVideoDDERR_BLTFASTCANTCLIP = 0x8876023e,
  kApVideoDDERR_NOBLTHW = 0x8876023f,
  kApVideoDDERR_NODDROPSHW = 0x88760240,
  kApVideoDDERR_OVERLAYNOTVISIBLE = 0x88760241,
  kApVideoDDERR_NOOVERLAYDEST = 0x88760242,
  kApVideoDDERR_INVALIDPOSITION = 0x88760243,
  kApVideoDDERR_NOTAOVERLAYSURFACE = 0x88760244,
  kApVideoDDERR_EXCLUSIVEMODEALREADYSET = 0x88760245,
  kApVideoDDERR_NOTFLIPPABLE = 0x88760246,
  kApVideoDDERR_CANTDUPLICATE = 0x88760247,
  kApVideoDDERR_NOTLOCKED = 0x88760248,
  kApVideoDDERR_CANTCREATEDC = 0x88760249,
  kApVideoDDERR_NODC = 0x8876024a,
  kApVideoDDERR_WRONGMODE = 0x8876024b,
  kApVideoDDERR_IMPLICITLYCREATED = 0x8876024c,
  kApVideoDDERR_NOTPALETTIZED = 0x8876024d,
  kApVideoDDERR_UNSUPPORTEDMODE = 0x8876024e,
};

static void SC3_CDECL ApVideoPlaybackThread(void *context);

// Yield or wait for the requested interval using the active platform's millisecond clock.
static void ApVideoDelay(u32 milliseconds) {
  SDL_Delay(milliseconds);
}

// Read the wrapping millisecond counter used by the original playback timing fields.
static u32 ApVideoTicks() {
  return static_cast<u32>(SDL_GetTicks());
}
// Exchange the shared movie state word with the same ordering on each platform.
static i32 ApVideoExchange(volatile i32 *word, i32 value) {
#if defined(_WIN32)
  return InterlockedExchange(reinterpret_cast<volatile LONG *>(word), value);
#else
  return __atomic_exchange_n(word, value, __ATOMIC_SEQ_CST);
#endif
}

// Read an SDL playback coordination word without changing its value.
static i32 ApVideoLoad(const volatile i32 *word) {
#if defined(_WIN32)
  return InterlockedCompareExchange(const_cast<volatile LONG *>(reinterpret_cast<const volatile LONG *>(word)), 0, 0);
#else
  return __atomic_load_n(word, __ATOMIC_SEQ_CST);
#endif
}

// Advance a shared queue or playback counter and return its new value.
static i32 ApVideoIncrement(volatile i32 *word) {
#if defined(_WIN32)
  return InterlockedIncrement(reinterpret_cast<volatile LONG *>(word));
#else
  return __atomic_add_fetch(word, 1, __ATOMIC_SEQ_CST);
#endif
}

// Decrease a shared queue counter and return its new value.
static i32 ApVideoDecrement(volatile i32 *word) {
#if defined(_WIN32)
  return InterlockedDecrement(reinterpret_cast<volatile LONG *>(word));
#else
  return __atomic_sub_fetch(word, 1, __ATOMIC_SEQ_CST);
#endif
}

// Publish the cleared playback state only after its resources have been closed.
static void *ApVideoExchangePointer(void *volatile *word, void *value) {
#if defined(_WIN32)
  return InterlockedExchangePointer(word, value);
#else
  return __atomic_exchange_n(word, value, __ATOMIC_SEQ_CST);
#endif
}

static int ApVideoSdlPlaybackThread(void *context) {
  ApVideoPlaybackThread(context);
  return 0;
}

// Start and detach the playback worker with the platform's original failure sentinel.
static uptr ApVideoStartPlaybackWorker(ApVideoInstance *instance) {
  SDL_Thread *worker = SDL_CreateThread(ApVideoSdlPlaybackThread, "movie playback", instance);
  uptr thread = worker ? reinterpret_cast<uptr>(worker) : static_cast<uptr>(-1);
  if (worker)
    SDL_DetachThread(worker);
  return thread;
}

// Start a movie on its playback thread, or run it synchronously when the thread flag is clear.
extern "C" uptr SC3_CDECL uV_Play_FromHandle(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  if (options->flags & kApVideoThreadedPlayback) {
    uptr thread = ApVideoStartPlaybackWorker(instance);
    options->playback_thread = thread;
    if (thread != static_cast<uptr>(-1)) {
      volatile i32 *status = (&options->playback_status);
      while (ApVideoExchange(status, 2) != 3)
        ApVideoDelay(20);
      ApVideoExchange(status, 3);
      return 0;
    }
    return thread;
  }
  ApVideoPlaybackThread(instance);
  return 1;
}

// Audio and video chunks occupy separate 24-entry queues in the vendor's movie state.
struct ApVideoChunk {
  u8 *data;
  i32 size, sequence, kind;
};

// Reached fields of the vendor's private movie state. Remaining offsets are left unnamed until recovered.
struct ApVideoState {
  SDL_AudioStream *sdl_audio;
  bool sdl_audio_initialized, sdl_audio_flushed;
  volatile i32 sdl_input_finished;
  u8 *sdl_frame;
  u32 sdl_pitch;
  ApVideoOptions *options;
  i32 screen_width, screen_height, render_width, render_height, render_x, render_y;
  u8 *video_storage, *audio_storage, *video_write, *audio_write;
  i32 front_unlock_errors, back_unlock_errors, overlay_back_unlock_errors, overlay_unlock_errors;
  i32 display_ready, video_flag_a, video_flag_b, video_depth, video_mode;
  i32 overlay_updated, has_attached_overlay;
  i32 channels, sample_rate, sample_bits, compression;
  ApVideoChunk audio_chunks[24], video_chunks[24];
  i32 audio_running, playback_buffer_size, audio_start_position, audio_read_slot, audio_write_slot, video_read_slot;
  volatile i32 video_queue_write, video_queue_count;
  volatile i32 audio_queue_count;
  volatile i32 video_sequence;
  i32 audio_packets;
  volatile i32 audio_frame_counter;
  i32 reserved920;
  i32 end_of_file;
  i32 audio_bytes, audio_threshold, frame_kind, chunk_result, last_chunk_result, current_audio_stream;
  i32 loop_offset, audio_tick, audio_thread_lock;
  uptr audio_thread;
  i32 audio_position, simulated_audio;
  i32 software_audio, primary_buffer_size, owns_primary_buffer, owns_sound_device;
  i32 alternate_playback;
  i32 owns_draw, left_sample, right_sample, previous_left_sample, previous_right_sample;
  i32 adpcm_samples, reserved1020, adpcm_steps;
  u8 reserved1028[4];
  i32 quantization[64];
  i32 dc_predictors[3];
  u8 reserved1300[244];
  const u8 *bitstream;
  u32 bit_position;
  u8 reserved1552[8];
  u32 start_tick, end_tick;
  u8 reserved1568[8];
  i32 first_color, color_count;
  i32 motion_count, literal_blocks, encoded_blocks, block_index_bits, motion_capacity, block_capacity;
  u8 *frame_buffers[7];
  u8 *old_frames[3];
};

static_assert(sizeof(uptr) != 4 || sizeof(ApVideoChunk) == 16);

// UltraVideo's fixed transform scaling and quantization tables.
static const i32 kApVideoTransformScale[64] = {8192, 5906, 6270, 6967, 8192, 10426, 15137, 29692, 5906, 4258, 4520,
    5023, 5906, 7517, 10913, 21407, 6270, 4520, 4799, 5332, 6270, 7980, 11585, 22725, 6967, 5023, 5332, 5925, 6967,
    8867, 12873, 25251, 8192, 5906, 6270, 6967, 8192, 10426, 15137, 29692, 10426, 7517, 7980, 8867, 10426, 13270, 19266,
    37791, 15137, 10913, 11585, 12873, 15137, 19266, 27969, 54864, 29692, 21407, 22725, 25251, 29692, 37791, 54864,
    107619};
static const i32 kApVideoQuantization[64] = {8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26,
    27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};
static i32 ApVideoTransformWeights[64];
static u32 ApVideoLumaTable[128], ApVideoChromaRedTable[128], ApVideoChromaBlueTable[128];
static const u8 *ApVideoBitSource;
static u32 ApVideoBitValue, ApVideoBitCount;
static u32 ApVideoDcValue;
static u32 ApVideoPrimaryTable[512], ApVideoSecondaryTable[256], ApVideoTertiaryTable[256];
static i32 ApVideoShortTable[64];
static bool ApVideoCodeTablesReady;
static i32 ApVideoOldQuantization[64];
static i32 ApVideoDifferentialFrame;
static i32 ApVideoOldCoefficients[64];
static i32 ApVideoOldLuma[256], ApVideoOldChroma[128];
static bool ApVideoOldFramesInitialized;

// Build the older movie decoder's variable-length lookup tables from its original code descriptors.
[[maybe_unused]] static i32 ApVideoInitializeCodeTables() {
  ApVideoPrimaryTable[0] = 15;
  for (i32 i = 1; i < 8; ++i)
    ApVideoPrimaryTable[i] = 31;
  for (i32 i = 8; i < 16; ++i)
    ApVideoPrimaryTable[i] = 47;
  for (i32 i = 256; i < 384; ++i)
    ApVideoPrimaryTable[i] = 63;
  for (const ApVideoCode &code : kApVideoPrimaryCodes) {
    bool primary = (code.bits & 0xfc00) != 0;
    u32 index = primary ? code.bits >> 7 : code.bits >> 2;
    u32 count = 1u << ((primary ? 9 : 14) - code.length);
    u32 value = (primary ? code.length : code.length - 6) |
        (((code.value << 16) | (code.value & 0xfc00)) << 6);
    u32 *table = primary ? ApVideoPrimaryTable : ApVideoTertiaryTable;
    for (u32 i = 0; i < count; ++i)
      table[index + i] = value;
  }
  for (const ApVideoCode &code : kApVideoSecondaryCodes) {
    bool tertiary = (code.bits & 0x8000) != 0;
    u32 index = tertiary ? code.bits >> 10 : code.bits >> 7;
    u32 count = 1u << ((tertiary ? 6 : 9) - code.length);
    u32 value = (tertiary ? code.length + 2 : code.length - 1) |
        (((code.value << 16) | (code.value & 0xfc00)) << 6);
    u32 *table = tertiary ? ApVideoTertiaryTable : ApVideoSecondaryTable;
    for (u32 i = 0; i < count; ++i)
      table[index + i] = value;
  }
  for (i32 i = 0; i < 32; ++i)
    ApVideoShortTable[i] = 1;
  for (i32 i = 32; i < 48; ++i)
    ApVideoShortTable[i] = static_cast<i32>((static_cast<u32>(i - 31) << 22) | 6);
  for (i32 i = 48; i < 64; ++i)
    ApVideoShortTable[i] = static_cast<i32>((static_cast<u32>(i - 64) << 22) | 6);
  ApVideoCodeTablesReady = true;
  return 0;
}

// Initialize the older frame decoder's bit reservoir and its quality-dependent transform coefficients.
[[maybe_unused]] static i32 ApVideoBeginOldFrame(const u16 *input, i32 differential, i32 quality) {
  if (!ApVideoCodeTablesReady)
    ApVideoInitializeCodeTables();
  u16 words[2];
  memcpy(words, input, sizeof(words));
  ApVideoBitValue = (static_cast<u32>(words[0]) << 16) | words[1];
  ApVideoBitSource = reinterpret_cast<const u8 *>(input) + sizeof(words);
  ApVideoBitCount = 32;
  ApVideoDifferentialFrame = differential;
  ApVideoOldQuantization[0] = kApVideoTransformScale[0] * 4;
  i32 result = 0;
  for (i32 i = 1; i < 64; ++i) {
    u32 scaled = static_cast<u32>(kApVideoQuantization[i]) * static_cast<u32>(quality) << 12;
    i64 product = static_cast<i64>(static_cast<i32>(scaled)) * kApVideoTransformScale[i];
    result = static_cast<i32>((product + 32768) >> 16);
    ApVideoOldQuantization[i] = result;
  }
  return result;
}

// Consume bits from the older frame decoder's global reservoir and refill it from the next little-endian word.
[[maybe_unused]] static i32 ApVideoConsumeBits(i32 count) {
  assert(count >= 0 && count <= 31);
  u32 remaining = ApVideoBitCount - count;
  ApVideoBitCount = remaining;
  ApVideoBitValue <<= count;
  if (remaining < 16) {
    u16 next;
    memcpy(&next, ApVideoBitSource, sizeof(next));
    ApVideoBitSource += 2;
    ApVideoBitValue |= static_cast<u32>(next) << (16 - remaining);
    ApVideoBitCount = remaining + 16;
  }
  return static_cast<i32>(ApVideoBitCount);
}

// Decode the older frame format's short signed value and consume its encoded bit length.
[[maybe_unused]] static i32 ApVideoReadShortValue() {
  i32 value = ApVideoShortTable[ApVideoBitValue >> 26];
  ApVideoConsumeBits(static_cast<u8>(value));
  return value >> 22;
}

// Fill an eight-by-eight transform block with the current DC value and return the advanced output pointer.
[[maybe_unused]] static u32 *ApVideoFillDcBlock(u32 *output, i32 stride) {
  for (i32 row = 0; row < 8; ++row) {
    for (i32 column = 0; column < 8; ++column)
      output[column] = ApVideoDcValue;
    output += stride;
  }
  return output;
}

// Decode one older-format transform block into its original zigzag coefficient array.
[[maybe_unused]] static i32 ApVideoReadOldBlock() {
  ApVideoDcValue = static_cast<u32>(ApVideoOldQuantization[0]) *
      static_cast<u32>(static_cast<i32>(ApVideoBitValue) >> 24);
  ApVideoConsumeBits(8);
  memset(ApVideoOldCoefficients, 0, sizeof(ApVideoOldCoefficients));
  ApVideoOldCoefficients[0] = static_cast<i32>(ApVideoDcValue);
  i32 next = 1;
  for (;;) {
    u32 entry = ApVideoPrimaryTable[ApVideoBitValue >> 23];
    if (static_cast<i8>(entry) > 9) {
      if (static_cast<u8>(entry) < 0x20) {
        if (static_cast<u8>(entry) >= 0x10) {
          ApVideoConsumeBits(6);
          entry = ApVideoTertiaryTable[ApVideoBitValue >> 24];
        } else {
          ApVideoConsumeBits(9);
          entry = ApVideoSecondaryTable[ApVideoBitValue >> 24];
        }
      } else if (static_cast<u8>(entry) < 0x30) {
        ApVideoConsumeBits(6);
        entry = (ApVideoBitValue & kApVideoUpperHalfwordMask) | 0x10;
      } else {
        ApVideoConsumeBits(2);
        return next;
      }
    }
    i32 index = next + ((entry >> 16) & 0x3f);
    ApVideoConsumeBits(static_cast<u8>(entry));
    assert(index >= 1 && index < 64);
    u32 position = kApVideoTqiZigzag[63 - index];
    ApVideoOldCoefficients[position] = static_cast<i32>(static_cast<u32>(ApVideoOldQuantization[position]) *
        static_cast<u32>(static_cast<i32>(entry) >> 22));
    next = index + 1;
  }
}

// Pack the rounded and unrounded channel contributions into the two original 16-bit pixel lanes.
[[maybe_unused]] static i32 ApVideoPack16(i32 red, i32 green, i32 blue, i32, i32 red_bits, i32 green_bits,
    i32 blue_bits, i8 red_shift, i8 green_shift, i8 blue_shift) {
  assert(red_bits >= 1 && red_bits <= 7 && green_bits >= 1 && green_bits <= 7 && blue_bits >= 1 && blue_bits <= 7 &&
      red_shift >= 0 && red_shift <= 15 && green_shift >= 0 && green_shift <= 15 && blue_shift >= 0 &&
      blue_shift <= 15);
  i32 red_unit = 1 << (8 - red_bits), green_unit = 1 << (8 - green_bits), blue_unit = 1 << (8 - blue_bits);
  i32 raw_red = red / red_unit, raw_green = green / green_unit, raw_blue = blue / blue_unit;
  i32 red_limit = (1 << red_bits) - 1, green_limit = (1 << green_bits) - 1, blue_limit = (1 << blue_bits) - 1;
  i32 r = (red + (red >= 0 ? 1 : -1) * (1 << (7 - red_bits))) / red_unit;
  i32 g = (green + (green >= 0 ? 1 : -1) * (1 << (7 - green_bits))) / green_unit;
  i32 b = (blue + (blue >= 0 ? 1 : -1) * (1 << (7 - blue_bits))) / blue_unit;
  r = r < -red_limit ? -red_limit : r > red_limit ? red_limit : r;
  g = g < -green_limit ? -green_limit : g > green_limit ? green_limit : g;
  b = b < -blue_limit ? -blue_limit : b > blue_limit ? blue_limit : b;
  if (b < 0)
    --g;
  if (g < 0)
    --r;
  if (r < 0)
    --raw_blue;
  if (raw_blue < 0)
    --raw_green;
  if (raw_green < 0)
    --raw_red;
  u32 result = ((r & red_limit) << red_shift) | ((g & green_limit) << green_shift) |
      ((b & blue_limit) << blue_shift) | (static_cast<u32>(raw_green & green_limit) << (green_shift + 16)) |
      (static_cast<u32>(raw_red & red_limit) << (red_shift + 16)) |
      (static_cast<u32>(raw_blue & blue_limit) << (blue_shift + 16));
  if (red_bits + green_bits + blue_bits != 16)
    result |= kApVideoPacked16SpareBits;
  return static_cast<i32>(result);
}

// Pack three signed color contributions and propagate carries between adjacent byte lanes.
[[maybe_unused]] static i32 ApVideoPack32(i32 red, i32 green, i32 blue, i32 alpha) {
  if (blue < 0)
    --green;
  if (green < 0)
    --red;
  if (red < 0)
    --alpha;
  u32 result = static_cast<u8>(blue) | (static_cast<u32>(static_cast<u8>(green)) << 8) |
      (static_cast<u32>(static_cast<u8>(red)) << 16) | (static_cast<u32>(alpha) << 24);
  return static_cast<i32>(result);
}

// Build the 16-bit luma and chroma contribution tables for the selected display channel layout.
[[maybe_unused]] static i32 ApVideoBuildColorTables16(i32 alpha, i32 red_bits, i32 green_bits, i32 blue_bits,
    i32 red_shift, i32 green_shift, i32 blue_shift) {
  i32 result = 0;
  for (i32 i = 0; i < 128; ++i) {
    i32 odd = static_cast<i8>(2 * i + 1), value = odd + 128;
    ApVideoLumaTable[i] = ApVideoPack16(value, value, value, alpha, red_bits, green_bits, blue_bits,
        static_cast<i8>(red_shift), static_cast<i8>(green_shift), static_cast<i8>(blue_shift));
    i32 green = (-22550 * odd + 32768) >> 16, blue = (kApVideoBlueChromaQ16 * odd + 32768) >> 16;
    ApVideoChromaBlueTable[i] = ApVideoPack16(0, green, blue, 0, red_bits, green_bits, blue_bits,
        static_cast<i8>(red_shift), static_cast<i8>(green_shift), static_cast<i8>(blue_shift));
    i32 red = (91881 * odd + 32768) >> 16;
    green = (-46799 * odd + 32768) >> 16;
    result = ApVideoPack16(red, green, 0, 0, red_bits, green_bits, blue_bits,
        static_cast<i8>(red_shift), static_cast<i8>(green_shift), static_cast<i8>(blue_shift));
    ApVideoChromaRedTable[i] = result;
  }
  return result;
}

// Build the 32-bit luma and chroma contribution tables used by the full-color renderer.
[[maybe_unused]] static i32 ApVideoBuildColorTables32(i32 alpha) {
  i32 result = 0;
  for (i32 i = 0; i < 128; ++i) {
    i32 odd = static_cast<i8>(2 * i + 1), value = odd + 128;
    ApVideoLumaTable[i] = ApVideoPack32(value, value, value, alpha);
    i32 green = (-22550 * odd + 32768) >> 16, blue = (kApVideoBlueChromaQ16 * odd + 32768) >> 16;
    ApVideoChromaBlueTable[i] = ApVideoPack32(0, green, blue, 0);
    i32 red = (91881 * odd + 32768) >> 16;
    green = (-46799 * odd + 32768) >> 16;
    result = ApVideoPack32(red, green, 0, 0);
    ApVideoChromaRedTable[i] = result;
  }
  return result;
}

// Select the 16-bit or full-color conversion tables for the current movie display format.
static i32 ApVideoSetColorTables(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  ApVideoPixelFormat *format = &options->pixel_format;
  if (format->bits_per_pixel == 15 || format->bits_per_pixel == 16)
    return ApVideoBuildColorTables16(255, format->red_bits, format->green_bits, format->blue_bits,
        format->red_shift, format->green_shift, format->blue_shift);
  return ApVideoBuildColorTables32(255);
}

static u32 ApVideoColorIndex(i8 value) {
  i64 product = static_cast<i64>(value) * ApVideoTransformWeights[0];
  return static_cast<u32>(((product + 32768) >> 16) >> 1) & 0x7f;
}

// Paint four constant-color eight-by-eight quadrants into a 32-bit movie frame block.
[[maybe_unused]] static i32 ApVideoPaintBlock32(i8 y0, i8 y1, i8 y2, i8 y3, i8 cb, i8 cr, u8 *output,
    u32 stride) {
  u32 chroma = ApVideoChromaRedTable[ApVideoColorIndex(cr)] + ApVideoChromaBlueTable[ApVideoColorIndex(cb)];
  i8 luminance[4] = {y0, y1, y2, y3};
  u32 result = 0;
  for (i32 quadrant = 0; quadrant < 4; ++quadrant) {
    result = chroma + ApVideoLumaTable[ApVideoColorIndex(luminance[quadrant])];
    u8 *row = output + (quadrant >= 2 ? 8 * stride : 0) + (quadrant & 1 ? 32 : 0);
    for (i32 y = 0; y < 8; ++y) {
      for (i32 x = 0; x < 8; ++x)
        memcpy(row + x * 4, &result, 4);
      row += 4 * (stride >> 2);
    }
  }
  return static_cast<i32>(result);
}

// Paint the constant-color movie block into paired 16-bit display pixels.
// The assembly helper's return register is unused and its original return type is unresolved.
[[maybe_unused]] static void ApVideoPaintBlock16(i8 y0, i8 y1, i8 y2, i8 y3, i8 cb, i8 cr, u8 *output,
    u32 stride) {
  u32 chroma = ApVideoChromaRedTable[ApVideoColorIndex(cr)] + ApVideoChromaBlueTable[ApVideoColorIndex(cb)];
  i8 luminance[4] = {y0, y1, y2, y3};
  for (i32 quadrant = 0; quadrant < 4; ++quadrant) {
    u32 color = chroma + ApVideoLumaTable[ApVideoColorIndex(luminance[quadrant])];
    u32 pair = (color & 0xffff) | (color << 16);
    u8 *row = output + (quadrant >= 2 ? 8 * stride : 0) + (quadrant & 1 ? 16 : 0);
    for (i32 y = 0; y < 8; ++y) {
      for (i32 x = 0; x < 4; ++x)
        memcpy(row + x * 4, &pair, 4);
      row += 2 * (stride >> 1);
    }
  }
}

// Render constant-color blocks of the older sixteen-bit format.
// The original transform-coded path writes through an invalid address and is outside the defined domain.
[[maybe_unused]] static void ApVideoRenderBlocks16(ApVideoState *state, const u8 *input, u8 *output, u8 *, u32 pitch) {
  u8 *row = output;
  for (u32 y = 0; y < static_cast<u32>(state->options->video_height); y += 16) {
    u8 *destination = row;
    for (u32 x = 0; x < static_cast<u32>(state->options->video_width); x += 16) {
      u8 size = *input++;
      const i8 *block = reinterpret_cast<const i8 *>(input);
      if (size == 3)
        ApVideoPaintBlock16(block[0], block[0], block[0], block[0], block[1], block[2], destination, pitch);
      else if (size == 6)
        ApVideoPaintBlock16(block[0], block[1], block[2], block[3], block[4], block[5], destination, pitch);
      else if (size == 12)
        ApVideoPaintBlock16(block[0], block[2], block[4], block[6], block[8], block[10], destination, pitch);
      else if (size > 12)
        assert(false && "Original sixteen-bit transform writes through an invalid address");
      input += size;
      destination += 32;
    }
    row += 32 * (pitch >> 1);
  }
}

// Render constant-color blocks of the older thirty-two-bit format.
// The original transform-coded path writes through an invalid address and is outside the defined domain.
[[maybe_unused]] static void ApVideoRenderBlocks32(ApVideoState *state, const u8 *input, u8 *output, u8 *, u32 pitch) {
  u8 *row = output;
  for (u32 y = 0; y < static_cast<u32>(state->options->video_height); y += 16) {
    u8 *destination = row;
    for (u32 x = 0; x < static_cast<u32>(state->options->video_width); x += 16) {
      u8 size = *input++;
      const i8 *block = reinterpret_cast<const i8 *>(input);
      if (size == 3)
        ApVideoPaintBlock32(block[0], block[0], block[0], block[0], block[1], block[2], destination, pitch);
      else if (size == 6)
        ApVideoPaintBlock32(block[0], block[1], block[2], block[3], block[4], block[5], destination, pitch);
      else if (size == 12)
        ApVideoPaintBlock32(block[0], block[2], block[4], block[6], block[8], block[10], destination, pitch);
      else if (size > 12)
        assert(false && "Original thirty-two-bit transform writes through an invalid address");
      input += size;
      destination += 64;
    }
    row += 64 * (pitch >> 2);
  }
}

// Build the frame-quality-dependent transform weights consumed by the block renderer.
[[maybe_unused]] static u32 ApVideoSetTransformQuality(i32 quality) {
  i32 base = 22 * (100 - quality) / 100 + 8;
  i32 step = 50 * (100 - quality) / 100 + 10 - base;
  for (i32 row = 0; row < 8; ++row) {
    for (i32 column = 0; column < 8; ++column) {
      i32 index = row * 8 + column;
      i64 factor = static_cast<i64>(base + step * (row + column) / 14) * 65536;
      i64 product = factor * kApVideoTransformScale[index];
      ApVideoTransformWeights[index] = static_cast<i32>((product + 32768) >> 16);
    }
  }
  return static_cast<u32>(ApVideoTransformWeights[63]);
}

// Build the alternate quantization table for the requested video quality and set its DC coefficient.
[[maybe_unused]] static i32 ApVideoBuildQuantization(i32 *output, i32 quality) {
  if (quality == 100) {
    for (i32 i = 0; i < 64; ++i)
      output[i] = kApVideoTransformScale[i] * 8;
  } else {
    f32 factor = static_cast<f32>((107.0 - quality) * 0.0625);
    for (i32 i = 0; i < 64; ++i) {
      i64 fixed = static_cast<i64>(static_cast<f64>(kApVideoQuantization[i]) * factor * 65536.0);
      i32 scaled = static_cast<i32>(fixed);
      output[i] = static_cast<i32>((static_cast<i64>(scaled) * kApVideoTransformScale[i] + 32768) >> 16);
    }
  }
  output[0] = 0x10000;
  return 0x10000;
}

// Signal the audio worker to finish and wait until it acknowledges the stop request.
[[maybe_unused]] static i32 ApVideoStopAudioThread(ApVideoState *state) {
  if (!state->audio_thread)
    return 1;
  volatile i32 *lock = reinterpret_cast<volatile i32 *>(&state->audio_thread_lock);
  ApVideoExchange(lock, 0);
  while (ApVideoExchange(lock, 0) == 0)
    ApVideoDelay(10);
  return 0;
}

// Measure the current playback position relative to the start of the circular sound buffer.
// The DirectSound output begins with the receiver address because the original reuses its incoming argument slot.
[[maybe_unused]] static i32 ApVideoGetAudioPosition(ApVideoState *state) {
  u32 position;
  if (state->sdl_audio) {
    int queued = SDL_GetAudioStreamQueued(state->sdl_audio);
    if (queued < 0) {
      ApVideoExchange((&state->options->playback_status), 5);
      return -1;
    }
    return queued < state->playback_buffer_size ? state->playback_buffer_size - queued : 0;
  }
  assert(state->simulated_audio);
    state->audio_position += 882;
    if (state->audio_position >= state->playback_buffer_size)
      state->audio_position -= state->playback_buffer_size;
    position = static_cast<u32>(state->audio_position);
  if (state->audio_start_position >= static_cast<i32>(position))
    return static_cast<i32>(position + static_cast<u32>(state->playback_buffer_size) -
        static_cast<u32>(state->audio_start_position));
  return static_cast<i32>(position - static_cast<u32>(state->audio_start_position));
}

// Fill the next sound-buffer region from the queued sample chunk, or with silence when the queue is empty.
// SDL flushes the completed nonlooping stream once, allowing queued audio to drain without adding more silence.
[[maybe_unused]] static i32 ApVideoWriteAudio(ApVideoState *state, u32 available) {
  ApVideoChunk *chunk = &state->audio_chunks[state->audio_read_slot % 24];
  if (state->sdl_audio) {
    if (!ApVideoLoad(&state->audio_queue_count)) {
      if (ApVideoLoad(&state->sdl_input_finished) && !ApVideoLoad(&state->video_queue_count)) {
        if (!state->sdl_audio_flushed) {
          if (!SDL_FlushAudioStream(state->sdl_audio)) {
            ApVideoExchange(&state->options->playback_status, kApVideoStopRequested);
            return -1;
          }
          state->sdl_audio_flushed = true;
        }
        return 0;
      }
      u8 silence[4096]{};
      while (available) {
        u32 size = available < sizeof(silence) ? available : sizeof(silence);
        if (!SDL_PutAudioStreamData(state->sdl_audio, silence, static_cast<int>(size))) {
          ApVideoExchange((&state->options->playback_status), 5);
          return -1;
        }
        available -= size;
      }
      return ApVideoIncrement((&state->audio_frame_counter));
    }
    if (!SDL_PutAudioStreamData(state->sdl_audio, chunk->data, chunk->size)) {
      ApVideoExchange((&state->options->playback_status), 5);
      return -1;
    }
    ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->audio_read_slot));
    ApVideoExchange((&state->audio_frame_counter), chunk->sequence);
    i32 remaining = ApVideoDecrement((&state->audio_queue_count));
    chunk->data = nullptr;
    chunk->size = 0;
    return remaining;
  }
  assert(state->simulated_audio);
  if (!state->audio_queue_count) {
    state->audio_start_position = (state->audio_start_position + available) % state->playback_buffer_size;
    return ApVideoIncrement((&state->audio_frame_counter));
  }
  state->audio_start_position = (chunk->size + state->audio_start_position) % state->playback_buffer_size;
  ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->audio_read_slot));
  ApVideoExchange((&state->audio_frame_counter), chunk->sequence);
  i32 remaining = ApVideoDecrement((&state->audio_queue_count));
  chunk->data = nullptr;
  chunk->size = 0;
  return remaining;
}

// Apply the SDL worker priority in the worker itself; the Windows caller sets it on the new thread handle.
static void ApVideoPrepareAudioWorker() {
  SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
}

// Complete the native CRT worker when required by its thread entry contract.
static void ApVideoFinishWorker() {
}

// Poll the sound buffer and feed queued audio until the movie asks the worker to finish.
static void SC3_CDECL ApVideoAudioThread(void *context) {
  ApVideoState *state = static_cast<ApVideoState *>(context);
  ApVideoPrepareAudioWorker();
  ApVideoDelay(10);
  while (ApVideoExchange(reinterpret_cast<volatile i32 *>(&state->audio_thread_lock), 1) != 0) {
    if (state->audio_running) {
      i32 position = ApVideoGetAudioPosition(state);
      if (position >= state->audio_chunks[state->audio_read_slot % 24].size)
        ApVideoWriteAudio(state, position);
    }
    ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->audio_tick));
    ApVideoDelay(10);
  }
  ApVideoFinishWorker();
}

static int ApVideoSdlAudioThread(void *context) {
  ApVideoAudioThread(context);
  return 0;
}

// Create and prioritize the audio worker, preserving each platform's existing handle and failure conventions.
static i32 ApVideoStartAudioWorker(ApVideoState *state) {
  SDL_Thread *worker = SDL_CreateThread(ApVideoSdlAudioThread, "movie audio", state);
  if (!worker)
    return 1;
  state->audio_thread = reinterpret_cast<uptr>(worker);
  SDL_DetachThread(worker);
  return 0;
}

// Start the audio worker and request the original highest thread priority.
[[maybe_unused]] static i32 ApVideoStartAudioThread(ApVideoState *state) {
  ApVideoExchange(reinterpret_cast<volatile i32 *>(&state->audio_thread_lock), 1);
  return ApVideoStartAudioWorker(state);
}

// Copy and offset an eight-by-eight luma block from the original interleaved previous-frame layout.
[[maybe_unused]] static u8 *ApVideoCopyLumaBlock(u8 *input, i32 phase, i32 stride, u8 *output, i8 delta) {
  u8 *source = input + (phase ? 7 : 2);
  static const i8 phase_offsets[2][8] = {{1, 0, 5, 4, 9, 8, 13, 12}, {-5, 0, -1, 4, 3, 8, 7, 12}};
  for (i32 row = 0; row < 8; ++row) {
    for (i32 column = 0; column < 8; ++column)
      output[2 + 4 * column] = static_cast<u8>(source[phase_offsets[phase != 0][column]] + delta);
    source += 4 * stride;
    output += 64;
  }
  return source;
}

// Copy and offset an eight-by-eight chroma block from the original interleaved previous-frame layout.
[[maybe_unused]] static u8 *ApVideoCopyChromaBlock(u8 *input, i32 channel, i32 stride, u8 *output, i8 delta) {
  u8 *source = input + channel;
  for (i32 row = 0; row < 8; ++row) {
    for (i32 column = 0; column < 8; ++column)
      output[2 + 4 * column] = static_cast<u8>(source[4 * column] + delta);
    source += 8 * stride;
    output += 32;
  }
  return source;
}

// Decode a non-null QFS movie payload only when the caller enables decompression.
[[maybe_unused]] static void ApVideoDecompressFrame(const u8 *input, u8 *output, i32 enabled) {
  if (input && enabled)
    ScQfsDecompress(input, nullptr, output);
}

// Copy an indexed frame into the selected eight-, sixteen-, or thirty-two-bit movie output surface.
// The original routine's return register varies between branches and its source return type is unresolved.
[[maybe_unused]] static void ApVideoCopyIndexedFrame(ApVideoState *state, u8 *output, u8 *, u32 pitch) {
  ApVideoOptions *options = state->options;
  const u8 *source = state->frame_buffers[0];
  u32 width = options->video_width, height = options->video_height;
  if (options->pixel_format.bits_per_pixel == 8) {
    if (options->flags & kApVideoDoubleWidth) {
      u32 count = width * height;
      assert(count && !(count & 3));
      for (u32 i = 0; i < count; ++i) {
        *output++ = source[i];
        *output++ = source[i];
      }
    } else if (width) {
      // The original zero-width REP copies do not access either pointer.
      for (u32 y = 0; y < height; ++y) {
        memcpy(output, source, width);
        source += width;
        output += 4 * (pitch >> 2);
      }
    }
    return;
  }
  if (!width)
    return;
  u32 bytes_per_pixel = options->flags & kApVideoDoubleWidth ? 4 : 2;
  for (u32 y = 0; y < height; ++y) {
    u8 *row = output;
    for (u32 x = 0; x < width; x += 8) {
      for (u32 i = 0; i < 8; ++i) {
        u32 color = options->packed_palette[*source++];
        memcpy(output, &color, bytes_per_pixel);
        output += bytes_per_pixel;
      }
    }
    if (y + 1 < height) {
      u32 remaining = pitch - bytes_per_pixel * width;
      u32 written = bytes_per_pixel * ((width + 7u) & ~7u);
      // The original 32-bit addition wraps when the next destination row overlaps this one.
      u32 advance = written + (bytes_per_pixel == 4 ? 4 * (remaining >> 2) : 2 * (remaining >> 1));
      output = row + advance;
    }
  }
}

// Expand signed, little-endian packed fields into the motion-vector array from the last field to the first.
[[maybe_unused]] static i32 ApVideoUnpackSignedFields(i32 *output, const u8 *input, i32 count, i32 width) {
  assert(width >= 1 && width <= 31 && count >= 0);
  u32 mask = (1u << width) - 1, sign = 1u << (width - 1);
  u32 offset = static_cast<u32>(width) * static_cast<u32>(count - 1);
  for (i32 i = count; i > 0; --i) {
    u32 word;
    memcpy(&word, input + (offset >> 3), sizeof(word));
    u32 value = (word >> (offset & 7)) & mask;
    output[i - 1] = static_cast<i32>(value & sign ? value | ~mask : value);
    offset -= width;
  }
  return 0;
}

// Expand each four-color packed block into sixteen palette indices in reverse bit order.
[[maybe_unused]] static u8 ApVideoExpandIndexedBlocks(const u8 *input, u8 *output, i32 count) {
  // The original caller skips an empty group; the helper's return byte is undefined without a block.
  assert(count > 0);
  u8 result = 0;
  for (i32 block = 0; block < count; ++block) {
    u32 indices;
    memcpy(&indices, input + 4, sizeof(indices));
    for (i32 pixel = 15; pixel >= 0; --pixel) {
      result = input[indices & 3];
      output[pixel] = result;
      indices >>= 2;
    }
    input += 8;
    output += 16;
  }
  return result;
}

// Allocate indexed-frame working buffers and decompress the first complete frame.
[[maybe_unused]] static i32 ApVideoInitializeIndexedFrame(ApVideoState *state, const u8 *frame) {
  u32 width = static_cast<u32>(state->options->video_width), height = static_cast<u32>(state->options->video_height);
  u32 bytes = (width * height + 15u) & ~15u, row_bytes = 8u * height;
  i32 signed_bytes = static_cast<i32>(bytes);
  static const u8 release_order[] = {0, 1, 3, 2};
  for (u8 index : release_order) {
    free(state->frame_buffers[index]);
    state->frame_buffers[index] = nullptr;
  }
  state->frame_buffers[0] = static_cast<u8 *>(malloc(bytes));
  state->frame_buffers[1] = static_cast<u8 *>(malloc(bytes));
  state->frame_buffers[3] = static_cast<u8 *>(malloc(row_bytes));
  state->frame_buffers[2] = static_cast<u8 *>(malloc(static_cast<u32>(4 * (signed_bytes / 16))));
  if (!state->frame_buffers[0] || !state->frame_buffers[1] || !state->frame_buffers[3] || !state->frame_buffers[2])
    return 0;
  i32 *rows = reinterpret_cast<i32 *>(state->frame_buffers[3]);
  u32 offset = 0u - height * width, count = 2u * height;
  assert(count <= row_bytes / 4);
  for (u32 row = 0; row < count; ++row) {
    rows[row] = static_cast<i32>(offset);
    offset += width;
  }
  ApVideoDecompressFrame(frame + 3 * (state->color_count + 4), state->frame_buffers[0], 1);
  return 1;
}

// Expand unsigned packed block indices in the original low-bit-first order.
[[maybe_unused]] static i32 ApVideoUnpackBlockIndices(i32 *output, const u8 *input, i32 count, i32 width) {
  assert(width >= 0 && width <= 31 && count >= INT32_MIN + 4);
  u32 mask = (1u << width) - 1, offset = 0, result = 1u << width;
  i32 i = 0;
  for (; i + 4 <= count; i += 4) {
    u32 word;
    memcpy(&word, input + (offset >> 3), sizeof(word));
    u64 repeated = static_cast<u64>(word) | (static_cast<u64>(word) << 32);
    word = static_cast<u32>(repeated >> (offset & 7));
    output[i] = static_cast<i32>(word & mask);
    repeated = static_cast<u64>(word) | (static_cast<u64>(word) << 32);
    output[i + 1] = static_cast<i32>((repeated >> width) & mask);
    u32 second_offset = offset + 2 * width;
    memcpy(&word, input + (second_offset >> 3), sizeof(word));
    repeated = static_cast<u64>(word) | (static_cast<u64>(word) << 32);
    word = static_cast<u32>(repeated >> (second_offset & 7));
    output[i + 2] = static_cast<i32>(word & mask);
    repeated = static_cast<u64>(word) | (static_cast<u64>(word) << 32);
    result = static_cast<u32>(repeated >> width) & mask;
    output[i + 3] = static_cast<i32>(result);
    offset += 4 * width;
  }
  for (; i < count; ++i) {
    u32 word;
    memcpy(&word, input + (offset >> 3), sizeof(word));
    u64 repeated = static_cast<u64>(word) | (static_cast<u64>(word) << 32);
    result = static_cast<u32>(repeated >> (offset & 7)) & mask;
    output[i] = static_cast<i32>(result);
    offset += width;
  }
  return static_cast<i32>(result);
}

// Reconstruct four-by-four pixel blocks from the previous frame or the literal block table.
[[maybe_unused]] static void ApVideoRestoreBlocks(const i32 *indices, const u8 *previous, u8 *output,
    i32 motion_count, const i32 *motion_offsets, const u8 *blocks, i32 stride, i32 columns, i32 rows) {
  u8 *row = output;
  do {
    u8 *destination = row;
    i32 remaining = columns;
    do {
      i32 index = *indices++;
      const u8 *source;
      if (index < motion_count)
        source = previous + (destination - output) + motion_offsets[index];
      else
        source = blocks + 16 * (index - motion_count);
      for (i32 line = 0; line < 4; ++line)
        memcpy(destination + line * stride, source + line * (index < motion_count ? stride : 4), 4);
      destination += 4;
    } while (--remaining > 0);
    row = destination + 3 * stride;
  } while (--rows > 0);
}

// Decode a differential indexed frame using motion offsets, literal blocks, and packed block indices.
[[maybe_unused]] static i32 ApVideoDecodeIndexedFrame(ApVideoState *state, const u8 *frame) {
  u8 *swap = state->frame_buffers[1];
  state->frame_buffers[1] = state->frame_buffers[0];
  state->frame_buffers[0] = swap;
  u16 header[4];
  memcpy(header, frame, sizeof(header));
  state->motion_count = header[0];
  state->literal_blocks = header[1];
  state->encoded_blocks = header[2];
  state->block_index_bits = header[3];
  i32 motion = state->motion_count, blocks = state->literal_blocks + state->encoded_blocks;
  bool ready = true;
  if (motion) {
    if (motion > state->motion_capacity) {
      if (state->frame_buffers[5]) {
        free(state->frame_buffers[5]);
        state->frame_buffers[5] = nullptr;
        state->motion_capacity = 0;
      }
      if (state->frame_buffers[6]) {
        free(state->frame_buffers[6]);
        state->frame_buffers[6] = nullptr;
      }
    }
    if (!state->frame_buffers[5]) {
      state->frame_buffers[5] = static_cast<u8 *>(malloc(8 * motion));
      state->motion_capacity = motion;
    }
    if (!state->frame_buffers[6]) {
      state->frame_buffers[6] = static_cast<u8 *>(malloc(4 * motion));
      state->motion_capacity = motion;
    }
    if (!state->frame_buffers[5] || !state->frame_buffers[6])
      ready = false;
  }
  if (blocks) {
    if (blocks > state->block_capacity && state->frame_buffers[4]) {
      free(state->frame_buffers[4]);
      state->frame_buffers[4] = nullptr;
    }
    if (!state->frame_buffers[4]) {
      state->frame_buffers[4] = static_cast<u8 *>(malloc(16 * blocks));
      state->block_capacity = blocks;
    }
    if (!state->frame_buffers[4])
      ready = false;
  }
  if (!ready)
    return 0;
  i32 *vectors = reinterpret_cast<i32 *>(state->frame_buffers[5]);
  i32 *offsets = reinterpret_cast<i32 *>(state->frame_buffers[6]);
  i32 packed_size = ((20 * motion + 31) >> 3) & ~3;
  ApVideoUnpackSignedFields(vectors, frame + 12, 2 * motion, 10);
  for (i32 i = 0; i < motion; ++i) {
    const i32 *rows = reinterpret_cast<const i32 *>(state->frame_buffers[3]) + state->options->video_height;
    offsets[i] = vectors[2 * i] + rows[vectors[2 * i + 1]];
  }
  const u8 *data = frame + packed_size + 12;
  i32 literal_bytes = 16 * state->literal_blocks;
  if (literal_bytes)
    memcpy(state->frame_buffers[4], data, literal_bytes);
  data += literal_bytes;
  if (state->encoded_blocks)
    ApVideoExpandIndexedBlocks(data, state->frame_buffers[4] + literal_bytes, state->encoded_blocks);
  data += 8 * state->encoded_blocks;
  u32 index_count = static_cast<u32>(state->options->video_height * state->options->video_width) >> 4;
  if ((index_count * state->block_index_bits + 31) & ~31u)
    ApVideoUnpackBlockIndices(reinterpret_cast<i32 *>(state->frame_buffers[2]), data, index_count,
        state->block_index_bits);
  ApVideoRestoreBlocks(reinterpret_cast<i32 *>(state->frame_buffers[2]), state->frame_buffers[1],
      state->frame_buffers[0], motion, offsets, state->frame_buffers[4], state->options->video_width,
      state->options->video_width >> 2, state->options->video_height >> 2);
  return 1;
}

static i32 ApVideoDecodeNibble(u8 nibble, u8 shift, i32 first, i32 second, u8 predictor) {
  static const i32 coefficients1[4] = {0, 240, 460, 392};
  static const i32 coefficients2[4] = {0, 0, -208, -220};
  assert(predictor < 4);
  i32 signed_nibble = nibble < 8 ? nibble : static_cast<i32>(nibble) - 16;
  i64 sum = static_cast<i64>(coefficients1[predictor]) * first +
      static_cast<i64>(coefficients2[predictor]) * second + signed_nibble * (1 << (20 - shift)) + 128;
  assert(sum >= INT32_MIN && sum <= INT32_MAX);
  i32 sample = static_cast<i32>(sum) >> 8;
  return sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample;
}

static i32 ApVideoDifferentialEntry(i32 step, u32 nibble) {
  i32 delta = step >> 3;
  if (nibble & 1)
    delta += step >> 2;
  if (nibble & 2)
    delta += step >> 1;
  if (nibble & 4)
    delta += step;
  return nibble & 8 ? -delta : delta;
}

// Expand UltraVideo's stereo differential audio bytes and retain the packed sample and step histories.
[[maybe_unused]] static void ApVideoDecodeDifferential(const u8 *input, i16 *output, i32 count, i32 *samples,
    i32 *steps) {
  static const i32 step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
  };
  static const i32 adjustment[8] = {-64, -64, -64, -64, 128, 256, 384, 512};
  i32 value[2] = {static_cast<i16>(*samples), static_cast<i16>(static_cast<u32>(*samples) >> 16)};
  i32 index[2] = {static_cast<i16>(*steps), static_cast<i16>(static_cast<u32>(*steps) >> 16)};
  i32 frames = 0;
  do {
    u8 byte = *input++;
    for (i32 channel = 0; channel < 2; ++channel) {
      u8 nibble = channel ? byte & 15 : byte >> 4;
      i32 offset = index[channel] + 4 * nibble;
      assert(offset >= 0 && offset <= 5692);
      u32 entry = static_cast<u32>(offset) >> 2, shift = (static_cast<u32>(offset) & 3) * 8;
      u32 delta = static_cast<u32>(ApVideoDifferentialEntry(step_table[entry / 16], entry & 15));
      if (shift) {
        ++entry;
        u32 next = static_cast<u32>(ApVideoDifferentialEntry(step_table[entry / 16], entry & 15));
        delta = (delta >> shift) | (next << (32 - shift));
      }
      i64 sample = static_cast<i64>(value[channel]) + static_cast<i32>(delta);
      assert(sample >= INT32_MIN && sample <= INT32_MAX);
      value[channel] = static_cast<i32>(sample);
      if (value[channel] < -32768)
        value[channel] = -32768;
      if (value[channel] > 32767)
        value[channel] = 32767;
      *output++ = static_cast<i16>(value[channel]);
      index[channel] += adjustment[nibble & 7];
      if (index[channel] < 0)
        index[channel] = 0;
      if (index[channel] > 5632)
        index[channel] = 5632;
    }
  } while (++frames < count);
  *samples = static_cast<i32>(static_cast<u16>(value[0]) | (static_cast<u32>(static_cast<u16>(value[1])) << 16));
  *steps = static_cast<i32>(static_cast<u16>(index[0]) | (static_cast<u32>(static_cast<u16>(index[1])) << 16));
}

// Decode mono ADPCM blocks to duplicated stereo samples, retaining both predictor histories and output position.
[[maybe_unused]] static i32 *ApVideoDecodeMono(i32 count, i32 *position, i32 *first, i32 *second, const u8 *input,
    i16 **output) {
  const u8 *source = input + 15 * *position / 28;
  i16 *destination = *output;
  i32 current = *first, previous = *second;
  for (i32 block = 0; block < (count > 0 ? (count + 27) / 28 : 0); ++block) {
    *position += 28;
    u8 header = *source++, predictor = header >> 4, shift = header & 15;
    for (i32 pair = 0; pair < 14; ++pair) {
      u8 encoded = *source++;
      i32 next = ApVideoDecodeNibble(encoded >> 4, shift, current, previous, predictor);
      *destination++ = static_cast<i16>(next);
      *destination++ = static_cast<i16>(next);
      previous = next;
      next = ApVideoDecodeNibble(encoded & 15, shift, previous, current, predictor);
      *destination++ = static_cast<i16>(next);
      *destination++ = static_cast<i16>(next);
      current = next;
    }
  }
  *first = current;
  *second = previous;
  *output = destination;
  return second;
}

// Decode interleaved stereo ADPCM blocks, preserving the two predictor histories for each channel.
[[maybe_unused]] static i32 ApVideoDecodeStereo(i32 count, i32 *position, i32 *left, i32 *previous_left,
    i32 *right, i32 *previous_right, const u8 *input, i16 **output) {
  const u8 *source = input + 30 * *position / 28;
  i16 *destination = *output;
  i32 l = *left, r = *right, old_l = *previous_left, old_r = *previous_right;
  for (i32 block = 0; block < (count > 0 ? (count + 27) / 28 : 0); ++block) {
    *position += 28;
    u8 predictors = *source++, shifts = *source++;
    for (i32 pair = 0; pair < 14; ++pair) {
      for (i32 sample = 0; sample < 2; ++sample) {
        u8 encoded = *source++;
        i32 next_l = ApVideoDecodeNibble(encoded >> 4, shifts >> 4, l, old_l, predictors >> 4);
        i32 next_r = ApVideoDecodeNibble(encoded & 15, shifts & 15, r, old_r, predictors & 15);
        *destination++ = static_cast<i16>(next_l);
        *destination++ = static_cast<i16>(next_r);
        old_l = l;
        old_r = r;
        l = next_l;
        r = next_r;
      }
    }
  }
  *left = l;
  *right = r;
  *previous_left = old_l;
  *previous_right = old_r;
  *output = destination;
  return old_r;
}

static u32 ApVideoYuvLuma[512], ApVideoYuvBlue[512], ApVideoYuvRed[512];
static u32 ApVideoRedConversion[306], ApVideoGreenConversion[262], ApVideoBlueConversion[354];

// Look up one packed YUV contribution in the original three display-channel tables.
static u32 ApVideoConvertPackedColor(u32 packed) {
  u32 red = packed >> 23, green = (packed >> 11) & 0x1ff, blue = packed & 0x1ff;
  assert(red < 306 && green < 262 && blue < 354);
  return ApVideoRedConversion[red] + ApVideoGreenConversion[green] + ApVideoBlueConversion[blue];
}

// Convert packed YUV pairs to two 16-bit display pixels using the original channel tables.
[[maybe_unused]] static u32 ApVideoConvertYuv16(const u32 *input, u32 *output, i32 count) {
  assert(count > 0);
  u32 result = 0;
  do {
    u32 word = *input++;
    u32 chroma = ApVideoYuvRed[word & 255] + ApVideoYuvBlue[(word >> 8) & 255];
    u32 first = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[word >> 24]);
    u32 second = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[(word >> 16) & 255]);
    result = (first & 0xffff) | (second & kApVideoUpperHalfwordMask);
    *output++ = result;
  } while (--count);
  return result;
}

// Convert packed YUV pairs into separate full-color display pixels.
[[maybe_unused]] static i32 ApVideoConvertYuv32(const u32 *input, u32 *output, i32 count) {
  assert(count > 0);
  u32 result = 0;
  do {
    u32 word = *input++;
    u32 chroma = ApVideoYuvRed[word & 255] + ApVideoYuvBlue[(word >> 8) & 255];
    *output++ = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[word >> 24]);
    result = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[(word >> 16) & 255]);
    *output++ = result;
  } while (--count);
  return static_cast<i32>(result);
}

// Average two packed YUV rows before converting their sample pairs into full-color pixels.
[[maybe_unused]] static i32 ApVideoConvertYuvAverage(const u32 *first, const u32 *second, u32 *output, i32 count) {
  assert(count > 0);
  u32 result = 0;
  do {
    u32 word = ((*first++ + *second++) >> 1) & kApVideoByteAverageMask;
    u32 chroma = ApVideoYuvBlue[(word >> 8) & 255] + ApVideoYuvRed[word & 255];
    *output++ = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[word >> 24]);
    result = ApVideoConvertPackedColor(chroma + ApVideoYuvLuma[(word >> 16) & 255]);
    *output++ = result;
  } while (--count);
  return static_cast<i32>(result);
}

// Build one of the original brightness-adjusted channel conversion tables.
static void ApVideoBuildChannel(u32 *table, i32 start, i32 count, i32 gamma, i32 bits, i32 shift, i32 pixel_bits) {
  assert(bits >= 0 && bits <= 8 && shift >= 0 && shift <= 31 && (pixel_bits > 16 || shift <= 15));
  for (i32 i = 0; i < count; ++i) {
    i64 product = static_cast<i64>(start + 2 * i) * gamma;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    i32 value = static_cast<i32>(product) / 100;
    value = value < 0 ? 0 : value > 255 ? 255 : value;
    i32 raw = value >> (8 - bits);
    // An eight-bit channel needs no quantization; the original rounding shift is undefined for this width.
    i32 rounded = bits == 8 ? value : (value + (1 << (7 - bits))) >> (8 - bits);
    i32 maximum = (1 << bits) - 1;
    if (rounded > maximum)
      rounded = maximum;
    table[i] = static_cast<u32>(rounded) << shift;
    if (pixel_bits <= 16)
      table[i] += static_cast<u32>(raw) << (shift + 16);
  }
}

// Build the movie's YUV conversion and brightness-adjusted display channel tables.
[[maybe_unused]] static i32 ApVideoBuildPixelConversion(ApVideoPixelFormat *format, ApVideoState *state) {
  for (i32 value = -256; value < 256; ++value) {
    i32 channel = value < -64 ? -64 : value > 63 ? 63 : value;
    u32 index = static_cast<u32>(value - 192) & 0x1ff;
    f64 centered = static_cast<f64>(channel) + 0.5;
    ApVideoYuvLuma[index] = kApVideoPackedLumaScale * channel + kApVideoPackedLumaBias;
    u32 green = static_cast<u32>(static_cast<i64>(0.5 - centered * 0.3441)) & 0x1ff;
    u32 blue = static_cast<u32>(static_cast<i64>(0.5 + centered * 1.772)) & 0x1ff;
    ApVideoYuvBlue[index] = (green << 11) + blue;
    green = static_cast<u32>(static_cast<i64>(0.5 - centered * 0.7141)) & 0x1ff;
    u32 red = static_cast<u32>(static_cast<i64>(0.5 + centered * 1.402)) & 0x1ff;
    ApVideoYuvRed[index] = (green + (red << 12)) << 11;
  }
  i32 gamma = state->options->gamma;
  ApVideoBuildChannel(ApVideoRedConversion, -177, 306, gamma, format->red_bits, format->red_shift,
      format->bits_per_pixel);
  ApVideoBuildChannel(ApVideoGreenConversion, -133, 262, gamma, format->green_bits, format->green_shift,
      format->bits_per_pixel);
  ApVideoBuildChannel(ApVideoBlueConversion, -225, 354, gamma, format->blue_bits, format->blue_shift,
      format->bits_per_pixel);
  return 483;
}


// Start the movie's stereo sound buffer, falling back to the original simulated clock on device failure.
[[maybe_unused]] static i32 ApVideoStartSound(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  if (options->flags & kApVideoEnableAudio) {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq = state->sample_rate;
    state->sdl_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    bool started = state->sdl_audio != nullptr;
    if (started && (options->flags & kApVideoUseAudioVolume) && options->audio_volume >= -10000 && options->audio_volume <= 0)
      SDL_SetAudioStreamGain(state->sdl_audio, options->audio_volume == -10000 ? 0.0f :
          static_cast<f32>(pow(10.0, options->audio_volume / 2000.0)));
    if (started)
      started = SDL_ResumeAudioStreamDevice(state->sdl_audio);
    if (started) {
      state->playback_buffer_size = 0x8000;
      state->simulated_audio = 0;
      state->audio_start_position = 0;
      state->audio_running = 1;
      return 0;
    }
    if (state->sdl_audio) {
      SDL_DestroyAudioStream(state->sdl_audio);
      state->sdl_audio = nullptr;
    }
  }
  state->audio_position = 0;
  state->playback_buffer_size = 0x8000;
  state->simulated_audio = 1;
  state->audio_start_position = 0;
  state->audio_running = 1;
  return 0;
}

// Exchange the renderer's front and back surfaces or raw buffers after presenting a frame.
static ApVideoOptions *ApVideoSwapBuffers(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  return options;
}


// Close the initialized video renderer and release the buffers, overlay surfaces, and owned DirectDraw interface.
[[maybe_unused]] static void ApVideoCloseRendering(ApVideoState *state) {
  delete[] state->sdl_frame;
  state->sdl_frame = nullptr;
  for (u8 *&buffer : state->frame_buffers) {
    free(buffer);
    buffer = nullptr;
  }
  state->display_ready = 0;
  return;
}


// Release the movie's playback sound buffer and clear its options field.
[[maybe_unused]] static i32 ApVideoReleasePlaybackAudio(ApVideoState *state) {
  if (state->sdl_audio) {
    SDL_DestroyAudioStream(state->sdl_audio);
    state->sdl_audio = nullptr;
  }
  return 0;
}

// Release the primary sound buffer and device only when the movie player created them.
[[maybe_unused]] static i32 ApVideoCloseSound(ApVideoState *state) {
  ApVideoReleasePlaybackAudio(state);
  if (state->sdl_audio_initialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    state->sdl_audio_initialized = false;
  }
  return 0;
}

// Initialize the requested movie audio backend and record the primary-buffer capacity.
// An unavailable device retains the original silent-clock fallback.
[[maybe_unused]] static i32 ApVideoInitializeSound(ApVideoState *state) {
  if (!(state->options->flags & kApVideoEnableAudio))
    return 0;
  state->sdl_audio_initialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
  state->primary_buffer_size = 0x8000;
  return 0;
}


// Convert the indexed movie palette into the current display pixel layout.
[[maybe_unused]] static void ApVideoPackPalette(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  ApVideoPixelFormat *format = &options->pixel_format;
  assert(format->red_bits >= 0 && format->red_bits <= 8 && format->green_bits >= 0 && format->green_bits <= 8 &&
      format->blue_bits >= 0 && format->blue_bits <= 8 && format->red_shift >= 0 && format->red_shift <= 31 &&
      format->green_shift >= 0 && format->green_shift <= 31 && format->blue_shift >= 0 && format->blue_shift <= 31);
  for (i32 i = 0; i < 256; ++i) {
    const u8 *color = options->palette + 3 * i;
    u32 value = (static_cast<u32>(color[0] >> (8 - format->red_bits)) << format->red_shift) |
        (static_cast<u32>(color[1] >> (8 - format->green_bits)) << format->green_shift) |
        (static_cast<u32>(color[2] >> (8 - format->blue_bits)) << format->blue_shift);
    if (options->flags & kApVideoDoubleWidth)
      value |= value << 16;
    options->packed_palette[i] = value;
  }
}


static u32 ApVideoClipTable[1024];

static u32 ApVideoTqiWindow(ApVideoState *state) {
  u32 words[2];
  memcpy(words, state->bitstream + 4 * (state->bit_position >> 5), sizeof(words));
  u32 shift = state->bit_position & 31;
  return shift ? (words[0] << shift) | (words[1] >> (32 - shift)) : words[0];
}

// Decode one TQI DC predictor using the recovered luma or chroma variable-length table.
[[maybe_unused]] static i32 ApVideoTqiReadDc(ApVideoState *state, i32 channel) {
  u32 window = ApVideoTqiWindow(state);
  u8 code = channel ? kApVideoTqiChromaDcTable[window >> 24] : kApVideoTqiLumaDcTable[window >> 25];
  state->bit_position += code & 15;
  u32 width = code >> 4;
  i32 difference = 0;
  if (width) {
    u32 value = ApVideoTqiWindow(state) >> (32 - width);
    state->bit_position += width;
    difference = static_cast<i32>(value);
    if (!(value & (1u << (width - 1))))
      difference -= (1u << width) - 1;
  }
  state->dc_predictors[channel] = static_cast<i32>(static_cast<u32>(state->dc_predictors[channel]) +
      static_cast<u32>(difference));
  return state->dc_predictors[channel];
}

// Decode the variable-length AC coefficients of one TQI block into its quantized transform array.
[[maybe_unused]] static void ApVideoTqiReadCoefficients(ApVideoState *state, i32 dc, i32 *coefficients) {
  memset(coefficients, 0, 64 * sizeof(*coefficients));
  coefficients[0] = static_cast<i32>(static_cast<u32>(dc) * static_cast<u32>(state->quantization[0]));
  u32 remaining = 63;
  for (;;) {
    u32 bits = ApVideoTqiWindow(state) >> 15, table_index;
    if (bits >= 0x8000)
      table_index = bits >> 13;
    else if (bits >= 0x800)
      table_index = 12 + (bits >> 9);
    else if (bits >= 0x400)
      table_index = 68 + (bits >> 7);
    else if (bits >= 0x200)
      table_index = 68 + (bits >> 5);
    else if (bits >= 0x100)
      table_index = 84 + (bits >> 4);
    else if (bits >= 0x80)
      table_index = 100 + (bits >> 3);
    else if (bits >= 0x40)
      table_index = 116 + (bits >> 2);
    else if (bits >= 0x20)
      table_index = 132 + (bits >> 1);
    else
      break;
    u32 entry = kApVideoTqiAcTable[table_index], run = (entry >> 8) & 255;
    state->bit_position += entry >> 16;
    if (run == 65)
      break;
    i32 level;
    if (run > 65) {
      u32 escaped = ApVideoTqiWindow(state) >> 18;
      state->bit_position += 14;
      run = (escaped >> 8) + 1;
      level = escaped & 255;
      if (!level || level == 128) {
        u32 extension = ApVideoTqiWindow(state) >> 24;
        state->bit_position += 8;
        level = level == 128 ? static_cast<i32>(extension) - 256 : static_cast<i32>(extension);
      } else if (level > 128) {
        level -= 256;
      }
    } else {
      level = entry & 255;
      if (ApVideoTqiWindow(state) & kApVideoStreamTopBit)
        level = -level;
      ++state->bit_position;
    }
    assert(run <= remaining && remaining - run < 63);
    remaining -= run;
    u32 index = kApVideoTqiZigzag[remaining];
    coefficients[index] = static_cast<i32>(static_cast<u32>(state->quantization[index]) * static_cast<u32>(level));
  }
}
