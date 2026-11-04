#include <string.h>

#include "audio/wave_reader.h"
#include "common/path.h"

// RIFF waveform parsing identifies its container, form, PCM format and sample data chunks.
enum AuWaveChunkTag : u32 {
  kAuRiffContainerTag = 0x46464952,
  kAuWaveFormTag = 0x45564157,
  kAuWaveFormatChunkTag = 0x20746d66,
  kAuWaveDataChunkTag = 0x61746164,
};

// Read the little-endian PCM format fields shared by the wave and XA readers.
static bool AuReadPcmFormat(SDL_IOStream *file, WAVEFORMATEX *format) {
  u16 tag, channels, alignment, bits;
  u32 rate, bytes_per_second;
  if (!SDL_ReadU16LE(file, &tag) || !SDL_ReadU16LE(file, &channels) || !SDL_ReadU32LE(file, &rate) ||
      !SDL_ReadU32LE(file, &bytes_per_second) || !SDL_ReadU16LE(file, &alignment) || !SDL_ReadU16LE(file, &bits))
    return false;
  format->wFormatTag = tag;
  format->nChannels = channels;
  format->nSamplesPerSec = rate;
  format->nAvgBytesPerSec = bytes_per_second;
  format->nBlockAlign = alignment;
  format->wBitsPerSample = bits;
  format->cbSize = 0;
  return true;
}

// Find a chunk inside the declared RIFF extent, accounting for the even-byte chunk padding.
static bool AuFindRiffChunk(SDL_IOStream *file, u32 wanted, i64 end, u32 *length) {
  if (SDL_SeekIO(file, 12, SDL_IO_SEEK_SET) < 0)
    return false;
  while (SDL_TellIO(file) >= 0 && SDL_TellIO(file) + 8 <= end) {
    u32 tag, size;
    if (!SDL_ReadU32LE(file, &tag) || !SDL_ReadU32LE(file, &size))
      return false;
    i64 start = SDL_TellIO(file), next = start + size + (size & 1);
    if (next > end)
      return false;
    if (tag == wanted) {
      *length = size;
      return true;
    }
    if (SDL_SeekIO(file, next, SDL_IO_SEEK_SET) < 0)
      return false;
  }
  return false;
}

void AuWaveReader::DeleteThis() {
  delete this;
}

// Start with no MMIO handle, empty RIFF/data descriptors and a cleared concrete format and error word.
AuPcmReader::AuPcmReader() : handle(nullptr), data_offset(0), data_length(0), remaining(0), format{}, error(0) {
}

AuPcmReader::~AuPcmReader() {
  AuPcmReader::Close();
}

// Find the original PCM format and data chunks through MMIO, preserving retained handles and reached error codes.
// A successful later parse does not clear a previously stored error; Close is the operation that resets it.
u8 AuPcmReader::Open(const char *path) {
  if (handle) {
    error = 2;
    return 0;
  }
  ScString native = ScNativeFilePath(path);
  handle = SDL_IOFromFile(native.Data(), "rb");
  if (!handle) {
    error = 4;
    return 0;
  }
  u32 tag, size, kind, length;
  if (!SDL_ReadU32LE(handle, &tag) || !SDL_ReadU32LE(handle, &size) || !SDL_ReadU32LE(handle, &kind)) {
    error = 5;
    return 0;
  }
  if (tag != kAuRiffContainerTag || kind != kAuWaveFormTag || size < 4) {
    error = 3;
    return 0;
  }
  i64 end = static_cast<i64>(size) + 8;
  if (!AuFindRiffChunk(handle, kAuWaveFormatChunkTag, end, &length)) {
    error = 5;
    return 0;
  }
  if (length < 16) {
    error = 3;
    return 0;
  }
  if (!AuReadPcmFormat(handle, &format)) {
    error = 4;
    return 0;
  }
  if (format.wFormatTag != 1) {
    error = 3;
    return 0;
  }
  if (!AuFindRiffChunk(handle, kAuWaveDataChunkTag, end, &data_length)) {
    error = 5;
    return 0;
  }
  i64 offset = SDL_TellIO(handle);
  if (offset < 0 || offset > UINT32_MAX) {
    error = 5;
    return 0;
  }
  data_offset = static_cast<u32>(offset);
  remaining = data_length;
  return error == 0;
}

// Clear the stored error first, then close a retained handle while ignoring the platform close result.
u8 AuPcmReader::Close() {
  error = 0;
  if (!handle)
    return 0;
  SDL_CloseIO(handle);
  handle = nullptr;
  return 1;
}

// Publish the allocated range before reading, and compare the byte result with the live, already-reduced chunk size.
// The original can free the published range and still report success; retain its optional count output independently.
u8 AuPcmReader::ReadAll(u8 **out, u32 *read) {
  *out = new u8[remaining];
  u32 result = Read(*out, remaining);
  if (read)
    *read = result;
  if (result != remaining) {
    delete[] *out;
    *out = nullptr;
  }
  return 1;
}

// Deduct the requested extent before copying from successive MMIO windows and preserve partial progress on advance
// failure.
u32 AuPcmReader::Read(u8 *out, u32 requested) {
  if (!remaining)
    return 0;
  if (static_cast<i32>(requested) > static_cast<i32>(remaining))
    requested = remaining;
  remaining -= requested;
  return static_cast<u32>(SDL_ReadIO(handle, out, requested));
}

// Return to the first data chunk after the RIFF form tag. A failed seek or descent stores the original chunk error.
u8 AuPcmReader::Reset() {
  if (SDL_SeekIO(handle, data_offset, SDL_IO_SEEK_SET) == data_offset) {
    remaining = data_length;
    return 1;
  }
  error = 5;
  return 0;
}

// Return the live data-chunk extent, which the read operation reduces before its platform copies.
u32 AuPcmReader::GetLength() {
  return remaining;
}

// Borrow the embedded concrete format without allocating a replacement or retaining the reader.
WAVEFORMATEX *AuPcmReader::GetFormat() {
  return &format;
}

u32 AuPcmReader::GetLastError() {
  return error;
}

// Preserve every initialized predictor lookup reachable by the original high header nibble, including adjacent tables.
static const f32 AuXaFirst[16] = {
    0, 0.9375f, 1.796875f, 1.53125f, 0, 0, -0.8125f, -0.859375f, 0, 4096, 8192, 12288, 16384, 20480, 24576, 28672};
static const f32 AuXaSecond[16] = {
    0, 0, -0.8125f, -0.859375f, 0, 4096, 8192, 12288, 16384, 20480, 24576, 28672, -32768, -28672, -24576, -20480};

static f64 AuXaNibble(u32 shift, u32 nibble) {
  i32 value = nibble < 8 ? static_cast<i32>(nibble) : static_cast<i32>(nibble) - 16;
  return shift < 13 ? value * (1 << (12 - shift)) : 0;
}

// Materialize the original double-rounding store before extracting its low word, preserving unsigned wrapping.
static i32 AuXaRounded(f64 value) {
  volatile f64 rounded = value + kScDoubleIntegerBias;
  f64 stored = rounded;
  u64 bits;
  memcpy(&bits, &stored, sizeof(bits));
  return static_cast<i32>(static_cast<u32>(bits) + kScDoubleIntegerLowWordBias);
}

AuXaReader::AuXaReader()
    : buffer{}, cursor(0), handle(nullptr),
      available(0),
      coefficients{}, history{}, nibbles{}, shifts{}, header{}, format{},
      error(0), remaining(0) {
}

AuXaReader::~AuXaReader() {
  AuXaReader::Close();
}

// Read the XA header and PCM format with the original null-handle test and Boolean ReadFile gates.
// Preserve the retained header on short reads and zero local format bytes before the original unchecked reads.
u8 AuXaReader::Open(const char *path) {
  ScString native = ScNativeFilePath(path);
  handle = SDL_IOFromFile(native.Data(), "rb");
  if (!handle) {
    error = 1;
    return 0;
  }
  if (!SDL_ReadU32LE(handle, &header.tag) || !SDL_ReadU32LE(handle, &header.length)) {
    error = 4;
    SDL_CloseIO(handle);
    handle = nullptr;
    return 0;
  }
  if ((header.tag & 0xffffu) != 0x4158u) {
    error = 3;
    SDL_CloseIO(handle);
    handle = nullptr;
    return 0;
  }
  if (!AuReadPcmFormat(handle, &format)) {
    error = 4;
    SDL_CloseIO(handle);
    handle = nullptr;
    return 0;
  }
  if (format.wFormatTag != 1) {
    error = 3;
    SDL_CloseIO(handle);
    handle = nullptr;
    return 0;
  }
  remaining = header.length;
  cursor = sizeof(buffer);
  available = 0;
  return 1;
}

// Reset the stored error before testing the handle, then ignore CloseHandle's result and clear the retained word.
u8 AuXaReader::Close() {
  error = 0;
  if (!handle)
    return 0;
  SDL_CloseIO(handle);
  handle = nullptr;
  return 1;
}

// Allocate and read the original WORD cbSize rather than the decoded length; the ordinary PCM format sets it to zero.
// The reached count is optional, and this entry reports success independently of the virtual reader result.
u8 AuXaReader::ReadAll(u8 **out, u32 *read) {
  *out = new u8[format.cbSize];
  u32 result = Read(*out, format.cbSize);
  if (read)
    *read = result;
  return 1;
}

// Decode the source's mono pairs or stereo frame pairs and preserve untouched remainder bytes in the supplied buffer.
// Failed packet reads return zero after earlier stores; successful completion deducts the full clamped request.
u32 AuXaReader::Read(u8 *out, u32 requested) {
  if (static_cast<i32>(remaining) < 1)
    return 0;
  if (static_cast<i32>(requested) > static_cast<i32>(remaining))
    requested = remaining;
  u32 left = requested;
  i16 discarded;
  if (format.nChannels == 1) {
    while (static_cast<i32>(left) >= 4) {
      if (!DecodeMono(reinterpret_cast<i16 *>(out), reinterpret_cast<i16 *>(out + 2)))
        return 0;
      left -= 4;
      out += 4;
    }
    if (left == 2 && !DecodeMono(reinterpret_cast<i16 *>(out), &discarded))
      return 0;
  } else if (format.nChannels == 2) {
    // The original stops at exactly eight bytes and leaves them unwritten (stale buffer contents); decode them.
    while (static_cast<i32>(left) >= 8) {
      if (!DecodeStereo(reinterpret_cast<i16 *>(out), reinterpret_cast<i16 *>(out + 2),
              reinterpret_cast<i16 *>(out + 4), reinterpret_cast<i16 *>(out + 6)))
        return 0;
      left -= 8;
      out += 8;
    }
    if (left == 4 &&
        !DecodeStereo(reinterpret_cast<i16 *>(out), reinterpret_cast<i16 *>(out + 2), &discarded, &discarded))
      return 0;
  } else
    return 0;
  remaining -= requested;
  return requested;
}

// Reset the compressed cursor and remaining decoded bytes before seeking to the fixed twenty-four-byte payload offset.
// A failed seek preserves those state writes and stores the original seek error.
u8 AuXaReader::Reset() {
  cursor = sizeof(buffer);
  remaining = header.length;
  available = 0;
  if (SDL_SeekIO(handle, 24, SDL_IO_SEEK_SET) == 24)
    return 1;
  error = 5;
  return 0;
}

u32 AuXaReader::GetLength() {
  return header.length;
}

// Borrow the actual embedded wave format, including its original zero cbSize field.
WAVEFORMATEX *AuXaReader::GetFormat() {
  return &format;
}

u32 AuXaReader::GetLastError() {
  return error;
}

// Fill the compressed window and reset its cursor after any successful platform read, including a short read.
// Failure stores the original file error and leaves the previous cursor and buffer effects visible.
u8 AuXaReader::Refill() {
  available = static_cast<u32>(SDL_ReadIO(handle, buffer, sizeof(buffer)));
  if (available) {
    cursor = 0;
    return 1;
  }
  error = 4;
  return 0;
}

// Decode two mono samples with the original predictor and rounded history sequence, then publish both WORD outputs.
u8 AuXaReader::DecodeMono(i16 *first, i16 *second) {
  if (cursor >= static_cast<i32>(sizeof(buffer)) && !Refill())
    return 0;
  if (static_cast<u32>(cursor) + (cursor % 15 == 0 ? 2u : 1u) > available)
    return 0;
  if (cursor % 15 == 0) {
    u32 predictor = buffer[cursor] >> 4;
    shifts[0] = predictor;
    coefficients[0] = AuXaFirst[predictor];
    coefficients[1] = AuXaSecond[predictor];
    shifts[0] = buffer[cursor] & 15;
    ++cursor;
  }
  f64 old = history[0], previous = history[1];
  nibbles[0] = buffer[cursor] >> 4;
  nibbles[1] = buffer[cursor] & 15;
  history[1] = AuXaRounded(previous * coefficients[1] + AuXaNibble(shifts[0], nibbles[0]) + old * coefficients[0]);
  history[0] = AuXaRounded(
      static_cast<f64>(history[1]) * coefficients[0] + AuXaNibble(shifts[0], nibbles[1]) + old * coefficients[1]);
  ++cursor;
  i16 sample = static_cast<i16>(history[1]);
  memcpy(first, &sample, sizeof(sample));
  sample = static_cast<i16>(history[0]);
  memcpy(second, &sample, sizeof(sample));
  return 1;
}

// Decode interleaved stereo pairs, preserving the left channel's intermediate float rounding and output-store order.
// Keep all packet nibbles and updated channel histories in the original concrete receiver fields.
u8 AuXaReader::DecodeStereo(i16 *firstLeft, i16 *firstRight, i16 *secondLeft, i16 *secondRight) {
  if (cursor >= static_cast<i32>(sizeof(buffer)) && !Refill())
    return 0;
  if (static_cast<u32>(cursor) + (cursor % 15 == 0 ? 4u : 2u) > available)
    return 0;
  if (cursor % 15 == 0) {
    shifts[0] = buffer[cursor] >> 4;
    shifts[1] = buffer[cursor + 1] >> 4;
    coefficients[0] = AuXaFirst[shifts[0]];
    coefficients[1] = AuXaSecond[shifts[0]];
    coefficients[2] = AuXaFirst[shifts[1]];
    coefficients[3] = AuXaSecond[shifts[1]];
    shifts[0] = buffer[cursor] & 15;
    shifts[1] = buffer[cursor + 1] & 15;
    cursor += 2;
  }
  volatile f32 oldLeft = static_cast<f32>(history[0]);
  f64 previous = history[1];
  nibbles[0] = buffer[cursor] >> 4;
  nibbles[1] = buffer[cursor] & 15;
  history[1] = AuXaRounded(
      previous * coefficients[1] + AuXaNibble(shifts[0], nibbles[0]) + static_cast<f64>(oldLeft) * coefficients[0]);
  history[0] = AuXaRounded(static_cast<f64>(history[1]) * coefficients[0] + AuXaNibble(shifts[0], nibbles[1]) +
      static_cast<f64>(oldLeft) * coefficients[1]);
  i16 sample = static_cast<i16>(history[1]);
  memcpy(firstLeft, &sample, sizeof(sample));
  sample = static_cast<i16>(history[0]);
  memcpy(secondLeft, &sample, sizeof(sample));
  f64 oldRight = history[2];
  previous = history[3];
  nibbles[2] = buffer[cursor + 1] >> 4;
  nibbles[3] = buffer[cursor + 1] & 15;
  history[3] = AuXaRounded(previous * coefficients[3] + AuXaNibble(shifts[1], nibbles[2]) + oldRight * coefficients[2]);
  history[2] = AuXaRounded(
      static_cast<f64>(history[3]) * coefficients[2] + AuXaNibble(shifts[1], nibbles[3]) + oldRight * coefficients[3]);
  sample = static_cast<i16>(history[3]);
  memcpy(firstRight, &sample, sizeof(sample));
  sample = static_cast<i16>(history[2]);
  memcpy(secondRight, &sample, sizeof(sample));
  cursor += 2;
  return 1;
}
