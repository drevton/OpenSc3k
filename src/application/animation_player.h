#ifndef SC3_APPLICATION_ANIMATION_PLAYER_H
#define SC3_APPLICATION_ANIMATION_PLAYER_H

#include "common/sc3.h"
#include "common/sc3_string.h"

// UltraVideo option bits retain their observed destination, playback, audio and resource-lifetime roles.
enum ApVideoOptionFlag : u32 {
  kApVideoDestinationX = 1,
  kApVideoDestinationY = 2,
  kApVideoDestinationPositionMask = 3,
  kApVideoCenterHorizontal = 4,
  kApVideoCenterVertical = 8,
  kApVideoDoubleHeight = 16,
  kApVideoScanlineDoubleHeight = 32,
  kApVideoDoubleHeightMask = 48,
  kApVideoDoubleWidth = 64,
  kApVideoDestinationWidth = 128,
  kApVideoDestinationHeight = 0x100,
  kApVideoDestinationSizeMask = 0x180,
  kApVideoAllowOverlay = 0x200,
  kApVideoDirectSurfaceDecode = 0x400,
  kApVideoRawBuffers = 0x800,
  kApVideoWaitForVerticalBlank = 0x1000,
  kApVideoUseAudioVolume = 0x2000,
  kApVideoSelectAudioTrack = 0x4000,
  kApVideoLoopPlayback = 0x8000,
  kApVideoThreadedPlayback = 0x10000,
  kApVideoThreadPriorityMask = 0x30000,
  kApVideoFrameCallback = 0x40000,
  kApVideoSuppliedFileHandle = 0x80000,
  kApVideoUseSuppliedGamma = 0x100000,
  kApVideoSuppliedSoundBuffer = 0x400000,
  kApVideoEnableAudio = 0x800000,
  kApVideoRestoreFrontSurface = 0x2000000,
  kApVideoSuppliedPalette = 0x4000000,
  kApVideoUseBackSurface = 0x8000000,
  kApVideoDefaultPlaybackFlags = 0x11400,
  kApVideoDefaultTargetFlags = 0x3000040,
};

// The playback state distinguishes opening, active/pause, stop requests, completion and released resources.
enum ApVideoPlaybackState : i32 {
  kApVideoInactive = 0,
  kApVideoReady = 1,
  kApVideoStarting = 2,
  kApVideoPlaying = 3,
  kApVideoPaused = 4,
  kApVideoStopRequested = 5,
  kApVideoFinished = 6,
  kApVideoClosed = 7,
};

class GxBuffer;
class GxColorType;
struct ScRect;
struct ApVideoInstance;
struct ApVideoOptions;

// Retail AnimationFileInfo type 0x2E95 and AnimationPlaySummaryInfo type 0x44CD.
struct ApAnimationFileInfo {
  u32 version, width, height, frames, frames_per_second, frames_per_second_fraction;
  u32 milliseconds_per_frame, milliseconds_per_frame_fraction, average_frame_size, largest_frame_size;
  u32 minimum_speed, file_size;
};

struct ApAnimationPlaySummary {
  u32 frames_per_second;
};

// Original cGZAnimationPlayer2 owns a platform UltraVideo options record and the selected movie path.
class ApAnimationPlayer {
public:
  enum class Destination : i32 { Unknown, Window, Buffer, PrimaryScreen, RawBuffer };
  ApAnimationPlayer();
  ApAnimationPlayer(const char *path, u32 width, u32 height);
  virtual ~ApAnimationPlayer();
  virtual bool Open(const char *path);
  virtual void Close();
  virtual void SetDestinationType(Destination value);
  virtual bool ComposeToGZBuffer(GxBuffer *buffer, u32 x, u32 y);
  virtual bool ComposeToRawBuffer(u8 *buffer, u32 width, u32 height, u32 x, u32 y);
  virtual bool SetVolume(i32 volume);
  virtual void SetLoopingPlayback(bool looping);
  virtual bool SetPalette();
  virtual bool Goto(u32 frame);
  virtual bool Stop();
  virtual bool Pause();
  virtual bool Resume();
  virtual void Reset();
  virtual void GetSummary(ApAnimationPlaySummary *out);
  virtual void CenterBufferInRect(ScRect *rectangle);
  virtual void SetPosition(i32 x, i32 y);
  virtual void SetSize(u32 width, u32 height);
  virtual void SetWindow(void *window);
  virtual bool SetZoomFactor(u32 factor);
  virtual bool SetFrameRate(u32 frame_rate);
  virtual void SetDestinationBufferColorType(GxBuffer *buffer);
  virtual void SetDestinationBufferColorType(const GxColorType &color);
  virtual u32 GetVideoWidth();
  virtual u32 GetVideoHeight();
  virtual u32 GetDestinationWidth();
  virtual u32 GetDestinationHeight();
  virtual bool GetInfo(ApAnimationFileInfo *out);
  static bool GetInfo(const ScString &path, ApAnimationFileInfo *out);
  static bool GetInfo(const char *path, ApAnimationFileInfo *out);
  static char *GetPlayerVersion(char *out);
  static void GetPlayerVersion(ScString *out);

protected:
  bool CanSetOptions();
  void SetOptions();
  void ConfigureOptionsSound();
  i32 status;
  bool opened;
  u32 pause_count;
  bool looping, blank_at_end;
  u32 fill_color, video_width, video_height;
  i32 destination_x, destination_y;
  u32 destination_width, destination_height;
  void *window;
  u32 buffer_blit_flags;
  Destination destination_type;
  i32 sound_volume;
  GxBuffer *front_buffer, *back_buffer;
  ApVideoInstance *video;
  ApVideoOptions *options;
  ScString path;
};

static_assert(sizeof(ApAnimationFileInfo) == 48);
static_assert(sizeof(ApAnimationPlaySummary) == 4);
static_assert(sizeof(uptr) != 4 || sizeof(ApAnimationPlayer) == 100);

struct ApVideoInstance;
struct ApVideoOptions;
struct IDirectDraw2;

extern "C" ApVideoInstance *SC3_CDECL uV_Open(char *filename, void *window, ApVideoOptions *options);

// UltraVideo's installed x86 exports use cdecl and return a 32-bit result; the player owns the opaque handle.
extern "C" uptr SC3_CDECL uV_Play_FromHandle(ApVideoInstance *instance);
extern "C" i32 SC3_CDECL uV_is_Playing(ApVideoInstance *instance);
extern "C" i32 SC3_CDECL uV_Close(ApVideoInstance *instance);
extern "C" i32 SC3_CDECL uV_Stop(ApVideoInstance *instance);
extern "C" i32 SC3_CDECL uV_Pause(ApVideoInstance *instance);
extern "C" i32 SC3_CDECL uV_SetAudioVolume(ApVideoInstance *instance, i32 volume);
extern "C" i32 SC3_CDECL uV_SetAudioFrequency(ApVideoInstance *instance, i32 frequency);
extern "C" i32 SC3_CDECL uV_SelectAudioTrack(ApVideoInstance *instance, i32 track);
extern "C" i32 SC3_CDECL uV_RenderTypes(IDirectDraw2 *device);
extern "C" i32 SC3_CDECL uV_SetGamma(ApVideoInstance *instance, i32 gamma);
extern "C" ApVideoInstance *SC3_CDECL uV_Play(char *filename, void *window, ApVideoOptions *options);
extern "C" i32 SC3_CDECL uV_Wait(ApVideoInstance *instance);
extern "C" char *SC3_CDECL uV_Version();

struct IUnknown;
struct IDirectDraw;
struct IDirectDraw2;
struct IDirectDrawSurface2;
struct IDirectSound;
struct IDirectSoundBuffer;
struct ApVideoState;
using ApVideoFrameCallback = i32 (SC3_CDECL *)();

struct ApVideoPixelFormat {
  i32 red_bits, green_bits, blue_bits;
  i32 red_shift, green_shift, blue_shift;
  u32 rgb_mask;
  i32 bits_per_pixel;
};

// Reached fields of the external UltraVideo options ABI. Unnamed bytes are owned by the vendor interface.
struct ApVideoOptions {
  ApVideoState *state;
  u32 flags, display_mode;
  IDirectDraw *surface_mode;
  IDirectDraw2 *draw2;
  IUnknown *front_surface, *back_surface;
  IDirectDrawSurface2 *overlay, *overlay_back;
  IUnknown *palette_object;
  u8 *palette_entries;
  ApVideoPixelFormat pixel_format;
  u8 *raw_front, *raw_back;
  IDirectSound *sound_device;
  IDirectSoundBuffer *sound_buffer;
  IDirectSoundBuffer *playback_audio;
  i32 video_height, video_width;
  i32 destination_x, destination_y;
  u32 destination_width, destination_height;
  i32 gamma, selected_audio_track, audio_tracks, has_audio, audio_only;
  i32 audio_volume, playback_elapsed, frames_displayed, frame_delay;
  volatile i32 playback_status;
  void *window;
  void *file_handle;
  i32 thread_priority;
  uptr playback_thread;
  ApVideoFrameCallback frame_callback;
  u8 palette[768];
  u32 packed_palette[256];
  GxBuffer *sdl_target;
};


// Tables from the installed UV.DLL TQI transform and variable-length decoder.
static constexpr u32 kApVideoTqiZigzag[63] = {
  0x0000003fu, 0x00000037u, 0x0000003eu, 0x0000003du, 0x00000036u, 0x0000002fu, 0x00000027u, 0x0000002eu,
  0x00000035u, 0x0000003cu, 0x0000003bu, 0x00000034u, 0x0000002du, 0x00000026u, 0x0000001fu, 0x00000017u,
  0x0000001eu, 0x00000025u, 0x0000002cu, 0x00000033u, 0x0000003au, 0x00000039u, 0x00000032u, 0x0000002bu,
  0x00000024u, 0x0000001du, 0x00000016u, 0x0000000fu, 0x00000007u, 0x0000000eu, 0x00000015u, 0x0000001cu,
  0x00000023u, 0x0000002au, 0x00000031u, 0x00000038u, 0x00000030u, 0x00000029u, 0x00000022u, 0x0000001bu,
  0x00000014u, 0x0000000du, 0x00000006u, 0x00000005u, 0x0000000cu, 0x00000013u, 0x0000001au, 0x00000021u,
  0x00000028u, 0x00000020u, 0x00000019u, 0x00000012u, 0x0000000bu, 0x00000004u, 0x00000003u, 0x0000000au,
  0x00000011u, 0x00000018u, 0x00000010u, 0x00000009u, 0x00000002u, 0x00000001u, 0x00000008u,
};
static constexpr u32 kApVideoTqiAcTable[164] = {
  0x00000009u, 0x00000002u, 0x00000001u, 0x00000008u, 0x00040102u, 0x00040301u, 0x00030201u, 0x00030201u,
  0x00024100u, 0x00024100u, 0x00024100u, 0x00024100u, 0x00020101u, 0x00020101u, 0x00020101u, 0x00020101u,
  0x00064200u, 0x00064200u, 0x00064200u, 0x00064200u, 0x00070302u, 0x00070302u, 0x00070a01u, 0x00070a01u,
  0x00070104u, 0x00070104u, 0x00070901u, 0x00070901u, 0x00060801u, 0x00060801u, 0x00060801u, 0x00060801u,
  0x00060701u, 0x00060701u, 0x00060701u, 0x00060701u, 0x00060202u, 0x00060202u, 0x00060202u, 0x00060202u,
  0x00060601u, 0x00060601u, 0x00060601u, 0x00060601u, 0x00080e01u, 0x00080106u, 0x00080d01u, 0x00080c01u,
  0x00080402u, 0x00080203u, 0x00080105u, 0x00080b01u, 0x00050103u, 0x00050103u, 0x00050103u, 0x00050103u,
  0x00050103u, 0x00050103u, 0x00050103u, 0x00050103u, 0x00050501u, 0x00050501u, 0x00050501u, 0x00050501u,
  0x00050501u, 0x00050501u, 0x00050501u, 0x00050501u, 0x00050401u, 0x00050401u, 0x00050401u, 0x00050401u,
  0x00050401u, 0x00050401u, 0x00050401u, 0x00050401u, 0x000a1101u, 0x000a0602u, 0x000a0107u, 0x000a0303u,
  0x000a0204u, 0x000a1001u, 0x000a0f01u, 0x000a0502u, 0x000c010bu, 0x000c0902u, 0x000c0503u, 0x000c010au,
  0x000c0304u, 0x000c0802u, 0x000c1601u, 0x000c1501u, 0x000c0109u, 0x000c1401u, 0x000c1301u, 0x000c0205u,
  0x000c0403u, 0x000c0108u, 0x000c0702u, 0x000c1201u, 0x000d0b02u, 0x000d0a02u, 0x000d0603u, 0x000d0404u,
  0x000d0305u, 0x000d0207u, 0x000d0206u, 0x000d010fu, 0x000d010eu, 0x000d010du, 0x000d010cu, 0x000d1b01u,
  0x000d1a01u, 0x000d1901u, 0x000d1801u, 0x000d1701u, 0x000e011fu, 0x000e011eu, 0x000e011du, 0x000e011cu,
  0x000e011bu, 0x000e011au, 0x000e0119u, 0x000e0118u, 0x000e0117u, 0x000e0116u, 0x000e0115u, 0x000e0114u,
  0x000e0113u, 0x000e0112u, 0x000e0111u, 0x000e0110u, 0x000f0128u, 0x000f0127u, 0x000f0126u, 0x000f0125u,
  0x000f0124u, 0x000f0123u, 0x000f0122u, 0x000f0121u, 0x000f0120u, 0x000f020eu, 0x000f020du, 0x000f020cu,
  0x000f020bu, 0x000f020au, 0x000f0209u, 0x000f0208u, 0x00100212u, 0x00100211u, 0x00100210u, 0x0010020fu,
  0x00100703u, 0x00101102u, 0x00101002u, 0x00100f02u, 0x00100e02u, 0x00100d02u, 0x00100c02u, 0x00102001u,
  0x00101f01u, 0x00101e01u, 0x00101d01u, 0x00101c01u,
};
static constexpr u8 kApVideoTqiLumaDcTable[128] = {
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43,
  0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x65, 0x65, 0x65, 0x65, 0x76, 0x76, 0x87, 0x00,
};
static constexpr u8 kApVideoTqiChromaDcTable[256] = {
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66, 0x77, 0x77, 0x88, 0x00,
};

// Original older-video variable-length code descriptors.
struct ApVideoCode { u32 length, value, bits; };
static constexpr ApVideoCode kApVideoPrimaryCodes[94] = {
  {3, 1, 0xc000u},
  {3, 1023, 0xe000u},
  {4, 1025, 0x6000u},
  {4, 2047, 0x7000u},
  {5, 2, 0x4000u},
  {5, 1022, 0x4800u},
  {5, 2049, 0x5000u},
  {5, 3071, 0x5800u},
  {6, 3, 0x2800u},
  {6, 1021, 0x2c00u},
  {6, 3073, 0x3800u},
  {6, 4095, 0x3c00u},
  {6, 4097, 0x3000u},
  {6, 5119, 0x3400u},
  {7, 1026, 0x1800u},
  {7, 2046, 0x1a00u},
  {7, 5121, 0x1c00u},
  {7, 6143, 0x1e00u},
  {7, 6145, 0x1400u},
  {7, 7167, 0x1600u},
  {7, 7169, 0x1000u},
  {7, 8191, 0x1200u},
  {8, 4, 0x0c00u},
  {8, 1020, 0x0d00u},
  {8, 2050, 0x0800u},
  {8, 3070, 0x0900u},
  {8, 8193, 0x0e00u},
  {8, 9215, 0x0f00u},
  {8, 9217, 0x0a00u},
  {8, 10239, 0x0b00u},
  {9, 5, 0x2600u},
  {9, 1019, 0x2680u},
  {9, 6, 0x2100u},
  {9, 1018, 0x2180u},
  {9, 1027, 0x2500u},
  {9, 2045, 0x2580u},
  {9, 3074, 0x2400u},
  {9, 4094, 0x2480u},
  {9, 10241, 0x2700u},
  {9, 11263, 0x2780u},
  {9, 11265, 0x2300u},
  {9, 12287, 0x2380u},
  {9, 12289, 0x2200u},
  {9, 13311, 0x2280u},
  {9, 13313, 0x2000u},
  {9, 14335, 0x2080u},
  {11, 7, 0x0280u},
  {11, 1017, 0x02a0u},
  {11, 1028, 0x0300u},
  {11, 2044, 0x0320u},
  {11, 2051, 0x02c0u},
  {11, 3069, 0x02e0u},
  {11, 4098, 0x03c0u},
  {11, 5118, 0x03e0u},
  {11, 5122, 0x0240u},
  {11, 6142, 0x0260u},
  {11, 14337, 0x0380u},
  {11, 15359, 0x03a0u},
  {11, 15361, 0x0340u},
  {11, 16383, 0x0360u},
  {11, 16385, 0x0200u},
  {11, 17407, 0x0220u},
  {13, 8, 0x01d0u},
  {13, 1016, 0x01d8u},
  {13, 9, 0x0180u},
  {13, 1015, 0x0188u},
  {13, 10, 0x0130u},
  {13, 1014, 0x0138u},
  {13, 11, 0x0100u},
  {13, 1013, 0x0108u},
  {13, 1029, 0x01b0u},
  {13, 2043, 0x01b8u},
  {13, 2052, 0x0140u},
  {13, 3068, 0x0148u},
  {13, 3075, 0x01c0u},
  {13, 4093, 0x01c8u},
  {13, 4099, 0x0120u},
  {13, 5117, 0x0128u},
  {13, 6146, 0x01e0u},
  {13, 7166, 0x01e8u},
  {13, 7170, 0x0150u},
  {13, 8190, 0x0158u},
  {13, 8194, 0x0110u},
  {13, 9214, 0x0118u},
  {13, 17409, 0x01f0u},
  {13, 18431, 0x01f8u},
  {13, 18433, 0x01a0u},
  {13, 19455, 0x01a8u},
  {13, 19457, 0x0190u},
  {13, 20479, 0x0198u},
  {13, 20481, 0x0170u},
  {13, 21503, 0x0178u},
  {13, 21505, 0x0160u},
  {13, 22527, 0x0168u},
};
static constexpr ApVideoCode kApVideoSecondaryCodes[128] = {
  {6, 12, 0xd000u},
  {6, 1012, 0xd400u},
  {6, 13, 0xc800u},
  {6, 1011, 0xcc00u},
  {6, 14, 0xc000u},
  {6, 1010, 0xc400u},
  {6, 15, 0xb800u},
  {6, 1009, 0xbc00u},
  {6, 1030, 0xb000u},
  {6, 2042, 0xb400u},
  {6, 1031, 0xa800u},
  {6, 2041, 0xac00u},
  {6, 2053, 0xa000u},
  {6, 3067, 0xa400u},
  {6, 3076, 0x9800u},
  {6, 4092, 0x9c00u},
  {6, 5123, 0x9000u},
  {6, 6141, 0x9400u},
  {6, 9218, 0x8800u},
  {6, 10238, 0x8c00u},
  {6, 10242, 0x8000u},
  {6, 11262, 0x8400u},
  {6, 22529, 0xf800u},
  {6, 23551, 0xfc00u},
  {6, 23553, 0xf000u},
  {6, 24575, 0xf400u},
  {6, 24577, 0xe800u},
  {6, 25599, 0xec00u},
  {6, 25601, 0xe000u},
  {6, 26623, 0xe400u},
  {6, 26625, 0xd800u},
  {6, 27647, 0xdc00u},
  {7, 16, 0x7c00u},
  {7, 1008, 0x7e00u},
  {7, 17, 0x7800u},
  {7, 1007, 0x7a00u},
  {7, 18, 0x7400u},
  {7, 1006, 0x7600u},
  {7, 19, 0x7000u},
  {7, 1005, 0x7200u},
  {7, 20, 0x6c00u},
  {7, 1004, 0x6e00u},
  {7, 21, 0x6800u},
  {7, 1003, 0x6a00u},
  {7, 22, 0x6400u},
  {7, 1002, 0x6600u},
  {7, 23, 0x6000u},
  {7, 1001, 0x6200u},
  {7, 24, 0x5c00u},
  {7, 1000, 0x5e00u},
  {7, 25, 0x5800u},
  {7, 999, 0x5a00u},
  {7, 26, 0x5400u},
  {7, 998, 0x5600u},
  {7, 27, 0x5000u},
  {7, 997, 0x5200u},
  {7, 28, 0x4c00u},
  {7, 996, 0x4e00u},
  {7, 29, 0x4800u},
  {7, 995, 0x4a00u},
  {7, 30, 0x4400u},
  {7, 994, 0x4600u},
  {7, 31, 0x4000u},
  {7, 993, 0x4200u},
  {8, 32, 0x3000u},
  {8, 992, 0x3100u},
  {8, 33, 0x2e00u},
  {8, 991, 0x2f00u},
  {8, 34, 0x2c00u},
  {8, 990, 0x2d00u},
  {8, 35, 0x2a00u},
  {8, 989, 0x2b00u},
  {8, 36, 0x2800u},
  {8, 988, 0x2900u},
  {8, 37, 0x2600u},
  {8, 987, 0x2700u},
  {8, 38, 0x2400u},
  {8, 986, 0x2500u},
  {8, 39, 0x2200u},
  {8, 985, 0x2300u},
  {8, 40, 0x2000u},
  {8, 984, 0x2100u},
  {8, 1032, 0x3e00u},
  {8, 2040, 0x3f00u},
  {8, 1033, 0x3c00u},
  {8, 2039, 0x3d00u},
  {8, 1034, 0x3a00u},
  {8, 2038, 0x3b00u},
  {8, 1035, 0x3800u},
  {8, 2037, 0x3900u},
  {8, 1036, 0x3600u},
  {8, 2036, 0x3700u},
  {8, 1037, 0x3400u},
  {8, 2035, 0x3500u},
  {8, 1038, 0x3200u},
  {8, 2034, 0x3300u},
  {9, 1039, 0x1300u},
  {9, 2033, 0x1380u},
  {9, 1040, 0x1200u},
  {9, 2032, 0x1280u},
  {9, 1041, 0x1100u},
  {9, 2031, 0x1180u},
  {9, 1042, 0x1000u},
  {9, 2030, 0x1080u},
  {9, 6147, 0x1400u},
  {9, 7165, 0x1480u},
  {9, 11266, 0x1a00u},
  {9, 12286, 0x1a80u},
  {9, 12290, 0x1900u},
  {9, 13310, 0x1980u},
  {9, 13314, 0x1800u},
  {9, 14334, 0x1880u},
  {9, 14338, 0x1700u},
  {9, 15358, 0x1780u},
  {9, 15362, 0x1600u},
  {9, 16382, 0x1680u},
  {9, 16386, 0x1500u},
  {9, 17406, 0x1580u},
  {9, 27649, 0x1f00u},
  {9, 28671, 0x1f80u},
  {9, 28673, 0x1e00u},
  {9, 29695, 0x1e80u},
  {9, 29697, 0x1d00u},
  {9, 30719, 0x1d80u},
  {9, 30721, 0x1c00u},
  {9, 31743, 0x1c80u},
  {9, 31745, 0x1b00u},
  {9, 32767, 0x1b80u},
};

#endif
