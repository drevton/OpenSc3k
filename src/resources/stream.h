#ifndef SC3_RESOURCES_STREAM_H
#define SC3_RESOURCES_STREAM_H

#include "common/sc3_string.h"
#include "resources/resource.h"

class ScVariant;

// Reached operations of the single record stream queried through IID00199627, preserving one Unknown identity.
// Operation order follows the installed table; serial transport uses verified original Boolean returns.
// OpenRecord publishes this primary DB-record prefix; QI4019960A still selects the distinct controlling record view.
class RsStream : public RsRecord {
public:
  virtual bool GetSize(u32 *out) = 0;                // Original +16, following the inherited GetKey +12.
  virtual bool ReadBytes(void *out, u32 length);     // Original +20.
  virtual bool ReadU8(u8 *out);                      // Original +24.
  virtual bool ReadI8Array(i8 *out, u32 count);      // Original +28.
  virtual bool ReadI8(i8 *out);                      // Original +32.
  virtual bool ReadU16Array(u16 *out, u32 count);    // Original +36.
  virtual bool ReadU16(u16 *out);                    // Original +40.
  virtual bool ReadI16Array(i16 *out, u32 count);    // Original +44.
  virtual bool ReadI16(i16 *out);                    // Original +48.
  virtual bool ReadU32Array(u32 *out, u32 count);    // Original +52.
  virtual bool ReadU32(u32 *out);                    // Original +56.
  virtual bool ReadF32Array(f32 *out, u32 count);    // Original +60.
  virtual bool ReadF32(f32 *out);                    // Original +64.
  virtual bool ReadI32Array(i32 *out, u32 count);    // Original +68.
  virtual bool ReadI32(i32 *out);                    // Original +72.
  virtual bool ReadI64Array(i64 *out, u32 count);    // Original +76.
  virtual bool ReadI64(i64 *out);                    // Original +80.
  virtual bool ReadString(ScStringView *text);       // Original +84.
  virtual bool ReadKey(RsKey *out);                  // Original +88.
  virtual bool ReadBlock(void *out, u32 length) = 0; // Original +92.
  // Typed variant transport uses the verified original Boolean return and preserves the source tag dispatch.
  virtual bool ReadVariant(ScVariant **out);                 // Original +96, GZResourceD1000B9F5.
  virtual bool WriteBytes(const void *data, u32 length);     // Original +100.
  virtual bool WriteU8(u8 value);                            // Original +104.
  virtual bool WriteI8Array(const i8 *source, u32 count);    // Original +108.
  virtual bool WriteI8(i8 value);                            // Original +112.
  virtual bool WriteU16Array(const u16 *source, u32 count);  // Original +116.
  virtual bool WriteU16(u16 value);                          // Original +120.
  virtual bool WriteI16Array(const i16 *source, u32 count);  // Original +124.
  virtual bool WriteI16(i16 value);                          // Original +128.
  virtual bool WriteU32Array(const u32 *source, u32 count);  // Original +132.
  virtual bool WriteU32(u32 value);                          // Original +136.
  virtual bool WriteF32Array(const f32 *source, u32 count);  // Original +140.
  virtual bool WriteF32(f32 value);                          // Original +144.
  virtual bool WriteI32Array(const i32 *source, u32 count);  // Original +148.
  virtual bool WriteI32(i32 value);                          // Original +152.
  virtual bool WriteI64Array(const i64 *source, u32 count);  // Original +156.
  virtual bool WriteI64(i64 value);                          // Original +160.
  virtual bool WriteString(const ScStringView *text);        // Original +164.
  virtual bool WriteKey(const RsKey *key);                   // Original +168.
  virtual bool WriteBlock(const void *data, u32 length) = 0; // Original +172.
  virtual bool WriteVariant(const ScVariant *value);             // Original +176, GZResourceD1000C2D6.
  virtual bool SkipBytes(u32 distance) = 0;                       // Original +180.
};

// Native text boundaries transcode only the supplied text using its evidenced legacy code page.
// Generic record strings remain raw byte spans; reference builds retain the original stream behavior.
bool RsReadLegacyText(RsStream *stream, ScStringView *text, u32 page);
bool RsReadLegacyText(RsStream *stream, ScStringView *text, u32 page, bool *conversion_valid);
bool RsWriteLegacyText(RsStream *stream, const ScStringView *text, u32 page);

#endif
