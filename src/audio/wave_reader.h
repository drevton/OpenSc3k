#ifndef SC3_AUDIO_WAVE_READER_H
#define SC3_AUDIO_WAVE_READER_H

#include <SDL3/SDL_iostream.h>
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

#include "common/sc3.h"

#if !(defined(_WIN32))
#pragma pack(push, 1)
struct tWAVEFORMATEX {
  u16 wFormatTag, nChannels;
  u32 nSamplesPerSec, nAvgBytesPerSec;
  u16 nBlockAlign, wBitsPerSample, cbSize;
};
#pragma pack(pop)
using WAVEFORMATEX = tWAVEFORMATEX;
#endif

// Common reader interface selected by the original sound loader, including its late deleting-destructor slot.
class AuWaveReader {
public:
  virtual void DeleteThis();
  virtual u8 Open(const char *path) = 0;
  virtual u8 Close() = 0;
  virtual u8 ReadAll(u8 **out, u32 *read) = 0;
  virtual u32 Read(u8 *out, u32 requested) = 0;
  virtual u8 Reset() = 0;
  virtual u32 GetLength() = 0;
  virtual WAVEFORMATEX *GetFormat() = 0;
  // Retire the abstract transport view after the concrete platform reader has closed its own handle.
  virtual ~AuWaveReader() = default;
  virtual u32 GetLastError() = 0;
};

// Original MMIO-backed PCM reader owns the RIFF/data descriptors and its concrete eighteen-byte wave format.
class AuPcmReader : public AuWaveReader {
public:
  AuPcmReader();
  ~AuPcmReader() override;
  u8 Open(const char *path) override;
  u8 Close() override;
  u8 ReadAll(u8 **out, u32 *read) override;
  u32 Read(u8 *out, u32 requested) override;
  u8 Reset() override;
  u32 GetLength() override;
  WAVEFORMATEX *GetFormat() override;
  u32 GetLastError() override;

private:
  friend class AuSample;
  SDL_IOStream *handle;
  u32 data_offset, data_length, remaining;
  WAVEFORMATEX format;
  u32 error;
};

// Original XA header stores the format tag separately from its decoded PCM byte length.
struct AuXaHeader {
  u32 tag, length;
};

// XA transport retains its actual compressed window, predictor coefficients and per-channel decoding history.
class AuXaReader : public AuWaveReader {
public:
  AuXaReader();
  ~AuXaReader() override;
  u8 Open(const char *path) override;
  u8 Close() override;
  u8 ReadAll(u8 **out, u32 *read) override;
  u32 Read(u8 *out, u32 requested) override;
  u8 Reset() override;
  u32 GetLength() override;
  WAVEFORMATEX *GetFormat() override;
  u32 GetLastError() override;

private:
  friend class AuSample;
  u8 Refill();
  u8 DecodeMono(i16 *first, i16 *second);
  u8 DecodeStereo(i16 *firstLeft, i16 *firstRight, i16 *secondLeft, i16 *secondRight);
  u8 buffer[3840];
  i32 cursor;
  SDL_IOStream *handle;
  u32 available;
  f32 coefficients[4];
  i32 history[4];
  u32 nibbles[4], shifts[2];
  AuXaHeader header;
  WAVEFORMATEX format;
  u32 error, remaining;
};

#endif
