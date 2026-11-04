#ifndef SC3_RESOURCES_BITMAP_RECORD_H
#define SC3_RESOURCES_BITMAP_RECORD_H

#include "resources/file.h"

struct HINSTANCE__;

// Actual RT_BITMAP stream with an owned BMP-file prefix and independent record reference count.
class RsBitmapRecord : public RsRecordStream {
public:
  RsBitmapRecord();
  ~RsBitmapRecord() override;
  bool GetSize(u32 *out) override;
  bool ReadBlock(void *out, u32 length) override;
  bool WriteBlock(const void *data, u32 length) override;
  virtual bool Init(HINSTANCE__ *module, u16 identifier);
  bool SkipBytes(u32 distance) override;

private:
  u8 *bytes;
  u32 size, position;
};

#endif
