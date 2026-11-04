#define _CRT_SECURE_NO_WARNINGS
#include "sprites/sprite.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <cstdlib>

#include "application/application_core.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "sprites/sp_definition_factory.h"

// Frame rendering distinguishes packed high-word effects from a low-word tint.
enum SpFrameEffectMask : u32 {
  kSpFrameHighWordEffectMask = 0xffff0000,
};

// Calendar instructions decode the begin and end month/day pairs from the decimal-packed argument.
enum SpScriptCalendarEncoding : u32 {
  kSpCalendarBeginMonthDivisor = 0xf4240,
};

// Fallback building images select one resource family indexed by zoom and footprint size.
enum SpGenericBuildingArtwork : u32 {
  kSpGenericBuildingImageBase = 0x1b870000,
};

// Packed sprite instances separate drawing effects, thirteen-bit references and two-bit rotation; script frames mark paired art.
enum SpInstancePackedField : u32 {
  kSpInstanceEffectMask = 0x1ffff,
  kSpInstanceReferenceMask = 0x3ffe0000,
  kSpInstanceWithoutRotation = 0x3fffffff,
  kSpScriptFramePairFlag = 0x1000000,
  kSpInstanceReferenceShift = 17,
  kSpInstanceReferenceValueMask = 0x1fff,
  kSpInstanceRotationShift = 30,
};

// The installed module constructs one recursive resource-factory region.
static ScLock SpFactoryRegion;

// Keep the real module-wide factory region locked through the original callback and release ordering.
class SpFactoryRegionGuard {
public:
  // Enter the same recursive region used by nested resource operations.
  SpFactoryRegionGuard() {
    SpFactoryRegion.Lock();
  }

  // Release the region on both ordinary and exceptional exits.
  ~SpFactoryRegionGuard() {
    SpFactoryRegion.Unlock();
  }
};

bool RsSpriteFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsFactoryInterface, RsFactoryInterface);
  return SpModuleObject::QueryInterface(iid, out);
}

u32 RsSpriteFactory::AddRef() {
  return SpModuleObject::AddRef();
}

u32 RsSpriteFactory::Release() {
  return SpModuleObject::Release();
}

// Create the record's resource type, copy its live key and publish the requested view only after a successful read.
bool RsSpriteFactory::Load(RsRecord *record, u32 iid, void **out) {
  SpFactoryRegionGuard region;
  void *object;
  bool result = false;
  if (New(record->GetKey()->type, IID_RsResource, &object)) {
    RsResource *resource = static_cast<RsResource *>(object);
    RsKey *source = record->GetKey(), *destination = resource->GetKey();
    *destination = *source;
    if (ReadRecord(resource, record))
      result = resource->QueryInterface(iid, out);
    resource->Release();
  }
  return result;
}

// Query the record's serial stream under the recursive region, then release it after the real resource reader returns.
bool RsSpriteFactory::ReadRecord(RsResource *resource, RsRecord *record) {
  SpFactoryRegionGuard region;
  void *object;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, &object)) {
    RsStream *stream = static_cast<RsStream *>(object);
    result = SerialRead(resource, stream);
    stream->Release();
  }
  return result;
}

// Query the record's serial stream under the same region and retain the writer result across the stream release.
bool RsSpriteFactory::WriteRecord(const RsResource *resource, RsRecord *record) {
  SpFactoryRegionGuard region;
  void *object;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, &object)) {
    RsStream *stream = static_cast<RsStream *>(object);
    result = SerialWrite(const_cast<RsResource *>(resource), stream);
    stream->Release();
  }
  return result;
}

u8 RsSpriteFactory::LockServer(u32) {
  return 0;
}

// Destroy the module-reference base after the concrete resource factory has finished its own lifetime.
RsSpriteFactory::~RsSpriteFactory() = default;

// Construct the sprite-attribute factory with its original unretained module-reference identity.
SpDefinitionFactory::SpDefinitionFactory() = default;

// The derived factory owns no additional fields; the shared base retires its module reference identity.
SpDefinitionFactory::~SpDefinitionFactory() = default;

bool SpDefinitionFactory::New(u32 type, u32 iid, void **out) {
  return SpGetCom()->GetClassObject(type, iid, out);
}

u8 SpDefinitionFactory::SerialRead(RsResource *resource, RsStream *stream) {
  void *object;
  if (!resource->QueryInterface(IID_SpDefView, &object))
    return 0;
  u8 result = static_cast<SpDefView *>(object)->Load(stream);
  resource->Release();
  return result;
}

u8 SpDefinitionFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  void *object;
  if (!resource->QueryInterface(IID_SpDefView, &object))
    return 0;
  u8 result = static_cast<SpDefView *>(object)->Write(stream);
  resource->Release();
  return result;
}

// Create class6500 and return its original secondary module-reference identity at zero holds.
ScUnknown *SpCreateDefinitionFactory() {
  return static_cast<SpModuleObject *>(new SpDefinitionFactory);
}

// Create the class6300 base sprite definition and return its primary definition identity without retaining it.
ScUnknown *SpCreateDefinition() {
  return static_cast<SpDefView *>(new SpDefinition);
}

// Create the class6301 animated sprite definition and return its primary definition identity without retaining it.
ScUnknown *SpCreateScriptDefinition() {
  return static_cast<SpDefView *>(new SpScriptDefinition);
}

// Create the class7315 scripted sprite and return its primary sprite identity without retaining it.
ScUnknown *SpCreateScriptSprite() {
  return static_cast<SpSprite *>(new SpScriptSprite);
}

// Create the class7300 static sprite and return its primary sprite identity without retaining it.
ScUnknown *SpCreateInstance() {
  return static_cast<SpSprite *>(new SpInstance);
}

// Create the class7312 layered sprite and return its primary sprite identity without retaining it.
ScUnknown *SpCreateLayerSprite() {
  return static_cast<SpSprite *>(new SpLayerSprite);
}

static bool SpIndexSpace(u8 value) {
  return value == ' ' || (value >= 9 && value <= 13);
}

// Scan one source-format integer with 32-bit modular arithmetic, retaining the bundled MSVCRT hex-width rule:
// a sign consumes width, whereas an optional 0x prefix does not. This replaces only the CRT scan primitive.
static bool SpIndexNumber(const char **cursor, u32 base, u32 width, u32 *out) {
  const char *text = *cursor;
  while (SpIndexSpace((u8)*text))
    ++text;
  bool negative = *text == '-';
  if (*text == '-' || *text == '+') {
    ++text;
    --width;
  }
  if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    text += 2;
  u32 value = 0, count = 0;
  while (width) {
    u8 byte = (u8)*text;
    u32 digit;
    if (byte >= '0' && byte <= '9')
      digit = byte - '0';
    else if (base == 16 && byte >= 'a' && byte <= 'f')
      digit = byte - 'a' + 10;
    else if (base == 16 && byte >= 'A' && byte <= 'F')
      digit = byte - 'A' + 10;
    else
      break;
    if (digit >= base)
      break;
    value = value * base + digit;
    ++text;
    ++count;
    --width;
  }
  if (!count)
    return false;
  *out = negative ? 0u - value : value;
  *cursor = text;
  return true;
}

// Match the literal punctuation and optional whitespace of a source sscanf format.
// Successful scans may leave trailing text, as the original format does not demand end-of-line.
static bool SpIndexMatch(const char **cursor, const char *format) {
  const char *text = *cursor;
  while (*format) {
    if (SpIndexSpace((u8)*format)) {
      do
        ++format;
      while (SpIndexSpace((u8)*format));
      while (SpIndexSpace((u8)*text))
        ++text;
    } else if (*text++ != *format++)
      return false;
  }
  *cursor = text;
  return true;
}

// Read the source CR-delimited line, discounting LF while retaining the source's contiguous substring calculation.
// A reached NUL stops without advancing past it; the output String owns its copied bytes and terminator.
u8 SpReadImageLine(const char **source, ScStringView *out) {
  const char *position = *source;
  u32 count = 0;
  u8 more = 1;
  for (;;) {
    char value = *position;
    if (value == '\r')
      break;
    if (value != '\n') {
      ++count;
      if (!value) {
        more = 0;
        break;
      }
    }
    ++position;
  }
  out->FromChar(position - count, count);
  *source = position + more;
  return more;
}

// Import version-zero/one SII records into the supplied map, retaining earlier insertions on a later bad line.
// Duplicate keys keep their first record; only the initial comment prefix is skipped, and no status output is added.
u8 SpImportImageMap(const char *source, SpImageMap *map, u32) {
  ScString line;
  u32 version = 0, count;
  do
    SpReadImageLine(&source, &line);
  while (line.Data()[0] == ';');
  const char *text = line.Data();
  if (strstr(text, "Version:")) {
    if (!SpIndexMatch(&text, "Version: ") || !SpIndexNumber(&text, 10, UINT32_MAX, &version))
      return 0;
    SpReadImageLine(&source, &line);
    text = line.Data();
  }
  if (version >= 2 || !SpIndexMatch(&text, "Record Count: ") || !SpIndexNumber(&text, 10, UINT32_MAX, &count))
    return 0;
  for (i32 i = 0; i < (i32)count; ++i) {
    SpReadImageLine(&source, &line);
    auto image = new SpImage;
    u32 fields[6], length = version ? 6 : 5;
    text = line.Data();
    u32 field = 0;
    for (; field < length; ++field) {
      bool hex = field < (version ? 2u : 1u);
      if (!SpIndexNumber(&text, hex ? 16 : 10, hex ? 8 : UINT32_MAX, &fields[field]))
        break;
      if (field + 1 < length && !SpIndexMatch(&text, ", "))
        break;
    }
    if (field != length) {
      delete image;
      return 0;
    }
    image->references = 0;
    image->buffer = nullptr;
    image->resource = fields[version ? 1 : 0];
    image->group = version ? fields[0] : 0;
    image->left = (i16)fields[version ? 2 : 1];
    image->right = (i16)fields[version ? 4 : 3];
    image->top = (i16)fields[version ? 3 : 2];
    image->bottom = (i16)fields[version ? 5 : 4];
    map->Insert(image->resource, image->group, image);
  }
  return 1;
}

// Read the complete SII file through the real file class, append its one sentinel byte, then invoke the importer.
// File destruction closes the handle; ordinary open/read failures return the original byte zero.
u8 SpReadImageMap(const ScStringView *path, SpImageMap *map, u32 argument) {
  RsFile file(*path);
  u8 result = 0;
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    u32 size = file.Size();
    auto bytes = new char[(usize)size + 1];
    bytes[size] = 0;
    if (file.Read(bytes, size) && SpImportImageMap(bytes, map, argument))
      result = 1;
    delete[] bytes;
  }
  return result;
}

// Copy packed WORD rows to the locked buffer, respecting its pitch. Convert the complete buffer after unlocking
// when the original format query differs, and preserve the original lock failure byte.
u8 SC3_STDCALL SpDecodeRaw(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height();
  u32 span = width * 2;
  for (u32 row = height; row; --row) {
    memcpy(pixels + destination, source, span);
    destination += pitch;
    source += span;
  }
  target->Unlock(GxLockFlags::Bits);
  if (different)
    target->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
  return 1;
}

// Decode byte-count/WORD packets across rows, converting each packet color before its stores. Zero-count packets
// still perform conversion, and a row's first packet is consumed even when its width is zero.
u8 SC3_STDCALL SpDecodeRowRuns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), span = width * 2;
  for (u32 row = height; row; --row) {
    usize end = destination + span;
    do {
      u8 count = source[0];
      u16 pixel;
      memcpy(&pixel, source + 1, sizeof(pixel));
      source += 3;
      if (different)
        target->Convert16BitValueFromFormatToFormat(
            &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
      for (u32 left = count; left; --left) {
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode the same packets down columns using pitch-sized steps. Byte offsets keep unused endpoints defined while
// preserving packet overshoot and the first packet when the computed height span is zero.
u8 SC3_STDCALL SpDecodeColumnRuns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(),
      span = pitch * height;
  for (u32 column = width; column; --column) {
    usize end = destination + span;
    do {
      u8 count = source[0];
      u16 pixel;
      memcpy(&pixel, source + 1, sizeof(pixel));
      source += 3;
      if (different)
        target->Convert16BitValueFromFormatToFormat(
            &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
      for (u32 left = count; left; --left) {
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Extract the prefix before a delimiter and consume that delimiter from the remaining text.
static ScString SpScriptToken(ScString *remaining, char delimiter) {
  const char *first = remaining->Data();
  const char *found = static_cast<const char *>(memchr(first, delimiter, remaining->Strlen()));
  if (!found)
    return ScString();
  u32 length = (u32)(found - first);
  ScString token(first, length);
  remaining->Erase(0, length + 1);
  return token;
}

// Compile the original comma-delimited animation language into shared tracks and optional calendar state.
// First-draw defaults retain full coordinates while draw opcodes preserve the original signed-low-word packing.
u8 SpScriptDefinition::CompileScript(const ScStringView *text) {
  SpScriptTrack *track = new SpScriptTrack;
  ScString remaining(*text), header = SpScriptToken(&remaining, ':');
  i32 zoom = 0, rotation = 0, origin_x = 0, origin_y = 0;
  i32 draw_x = 0, draw_y = 0, sequence[3] = {}, ping_pong[3] = {};
  u32 begin[2] = {}, end[2] = {};
  bool initial = false, has_calendar = false;
  sscanf(header.ToChar(), "Script[%" SCNd32 ",%" SCNd32 ",%" SCNd32 ",%" SCNd32 "]:", &zoom, &rotation, &origin_x,
      &origin_y);
  u32 group = (u32)zoom * 4 + (u32)rotation;
  for (;;) {
    ScString command = SpScriptToken(&remaining, ',');
    u32 spaces = 0;
    while (spaces < command.Strlen() && command.Data()[spaces] == ' ')
      ++spaces;
    command.Erase(0, spaces);
    if (!command.Strlen() || command.Data()[0] == 'E')
      break;
    const char *line = command.ToChar();
    if (*line == 'S' || *line == 'P') {
      i32 *values = *line == 'S' ? sequence : ping_pong;
      sscanf(line, *line == 'S' ? "S %" SCNd32 " %" SCNd32 " %" SCNd32 : "P %" SCNd32 " %" SCNd32 " %" SCNd32,
          &values[0], &values[1], &values[2]);
      u32 packed = (u32)origin_x << 16 | (u16)origin_y;
      i32 frame = values[0];
      for (; frame < values[0] + values[1]; ++frame) {
        SpScriptOp *draw = new SpScriptOp;
        draw->code = 1;
        draw->argument = (u32)frame;
        draw->extra = packed;
        *track->operations.Append() = draw;
        if (!initial) {
          *frames[group].Append() = draw->argument;
          *offset_x[group].Append() = (i16)origin_x;
          *offset_y[group].Append() = (i16)origin_y;
          initial = true;
        }
        SpScriptOp *wait = new SpScriptOp;
        wait->code = 3;
        wait->argument = (u32)values[2];
        *track->operations.Append() = wait;
      }
      if (*line == 'P') {
        while (--frame > values[0]) {
          SpScriptOp *draw = new SpScriptOp;
          draw->code = 1;
          draw->argument = (u32)frame;
          draw->extra = packed;
          *track->operations.Append() = draw;
          SpScriptOp *wait = new SpScriptOp;
          wait->code = 3;
          wait->argument = (u32)values[2];
          *track->operations.Append() = wait;
        }
      }
    } else if (*line == 'D') {
      SpScriptOp *draw = new SpScriptOp;
      i32 frame = 0;
      draw->code = line[1] == 'A' ? 2 : 1;
      sscanf(line, draw->code == 2 ? "DA %" SCNd32 " %" SCNd32 " %" SCNd32 : "D %" SCNd32 " %" SCNd32 " %" SCNd32,
          &frame, &draw_x, &draw_y);
      draw_x += (i16)origin_x;
      draw_y += (i16)origin_y;
      draw->argument = (u32)frame;
      draw->extra = (u32)draw_x << 16 | (u32)(i32)(i16)draw_y;
      *track->operations.Append() = draw;
      if (!initial) {
        *frames[group].Append() = draw->argument;
        *offset_x[group].Append() = draw_x;
        *offset_y[group].Append() = draw_y;
        initial = true;
      }
    } else if (*line == 'C') {
      if (line[1] == 'B') {
        i32 month = (i32)begin[0], day = (i32)begin[1];
        sscanf(line, "CB %" SCNd32 " %" SCNd32, &month, &day);
        begin[0] = (u32)month;
        begin[1] = (u32)day;
        has_calendar = true;
      } else if (line[1] == 'E') {
        i32 month = (i32)end[0], day = (i32)end[1];
        sscanf(line, "CE %" SCNd32 " %" SCNd32, &month, &day);
        end[0] = (u32)month;
        end[1] = (u32)day;
        has_calendar = true;
      } else if (line[1] == 'D')
        has_calendar = true;
    } else if (*line == 'R' || *line == 'G' || *line == 'L' || *line == 'W') {
      SpScriptOp *operation = new SpScriptOp;
      i32 argument = 0, extra = 0;
      const char *format;
      if (*line == 'R') {
        operation->code = 6;
        format = "R %" SCNd32;
      } else if (*line == 'L') {
        operation->code = 13;
        format = "L %" SCNd32;
      } else if (*line == 'G') {
        operation->code = line[1] == 'L' ? 14 : 5;
        format = line[1] == 'L' ? "GL %" SCNd32 : "G %" SCNd32;
      } else if (line[1] == 'R') {
        operation->code = 4;
        format = "WR %" SCNd32 " %" SCNd32;
      } else if (line[1] == 'D' || line[1] == 'L') {
        operation->code = line[1] == 'D' ? 11 : 12;
        format = line[1] == 'D' ? "WD %" SCNd32 " %" SCNd32 : "WL %" SCNd32 " %" SCNd32;
        has_calendar = true;
      } else {
        operation->code = 3;
        format = "W %" SCNd32;
      }
      sscanf(line, format, &argument, &extra);
      operation->argument = (u32)argument;
      if (operation->code == 4 || operation->code == 11 || operation->code == 12)
        operation->extra = (u32)extra;
      *track->operations.Append() = operation;
    }
  }
  *tracks[group].Append() = track;
  SpScriptCalendar *calendar = nullptr;
  if (has_calendar) {
    calendar = new SpScriptCalendar;
    calendar->begin[0] = begin[0];
    calendar->begin[1] = begin[1];
    calendar->end[0] = end[0];
    calendar->end[1] = end[1];
    calendar->active = 0;
    calendar->label = 0;
    calendar->target = 0;
  }
  *calendars[group].Append() = calendar;
  return 1;
}

// Read a child resource identifier and relative world position into the selected definition view.
// The descriptor begins without a live instance; view selection creates that instance later.
u8 SpScriptDefinition::ParseChild(const ScStringView *text) {
  SpScriptChild *child = new SpScriptChild;
  child->instance = nullptr;
  ScString source(*text);
  i32 zoom = 0, rotation = 0;
  sscanf(source.ToChar(), "Sprite[%" SCNd32 ",%" SCNd32 ",%08" SCNx32 ",%" SCNd32 ",%" SCNd32 ",%" SCNd32 "]", &zoom,
      &rotation, &child->resource, &child->position.x, &child->position.z, &child->position.altitude);
  *children[(u32)zoom * 4 + (u32)rotation].Append() = child;
  return 1;
}

SpScriptDefinition::SpScriptDefinition() {
}

// Free each view's operation records and track, then calendar records and child descriptors in original order.
// Definition child descriptors do not release sprite instances; runtime sprite ownership is handled separately.
SpScriptDefinition::~SpScriptDefinition() {
  for (u32 group = 0; group < 20; ++group) {
    for (u32 i = 0; i < tracks[group].size; ++i) {
      SpScriptTrack *track = tracks[group].data[i];
      for (u32 j = 0; j < track->operations.size; ++j)
        delete track->operations.data[j];
      delete track;
    }
    for (u32 i = 0; i < calendars[group].size; ++i)
      delete calendars[group].data[i];
    for (u32 i = 0; i < children[group].size; ++i)
      delete children[group].data[i];
  }
  for (u32 i = 0; i < countries.size; ++i)
    delete countries.data[i];
}

// Read country-selected script groups and the unconditional child list from the original binary resource stream.
// Matching country sections remain eligible after a default section; holiday sections require the city's actual flag.
u8 SpScriptDefinition::SerialReadBinary(RsStream *source) {
  ScString text, country_code;
  i32 country = -1;
  if (SpGetApplication())
    country = SpGetPreferences()->GetCountry();
  if (SpGetLanguageManager())
    SpGetLanguageManager()->GetISO3166CountryCodeFromCountryID(&country_code, (u32)country);
  u32 section_count, count;
  u8 result = source->ReadU32(&section_count);
  if (result) {
    ScString section;
    bool selected = false;
    for (u32 i = 0; i < section_count; ++i) {
      bool read = false;
      source->ReadString(&section);
      u32 length = section.Strlen();
      bool holidays = length >= 9 && !memcmp(section.Data() + length - 9, ":holidays", 9);
      if (holidays)
        section.Erase(length - 9, 9);
      if ((country_code.IsEqual(&section) ||
              (section.Strlen() == 3 && !memcmp(section.Data(), "def", 3) && !selected)) &&
          (!holidays || (SpGetCity() && SpGetCity()->AreHolidayFeaturesEnabled()))) {
        selected = true;
        read = true;
      }
      result = source->ReadU32(&count);
      if (result) {
        for (u32 j = 0; j < count; ++j) {
          if (result) {
            result = source->ReadString(&text);
            if (result && read)
              CompileScript(&text);
          }
        }
      }
    }
    if (result) {
      result = source->ReadU32(&count);
      if (result) {
        for (u32 i = 0; i < count; ++i) {
          if (result) {
            result = source->ReadString(&text);
            if (result)
              ParseChild(&text);
          }
        }
      }
    }
  }
  return result;
}

u8 SpScriptDefinition::Write(RsStream *) {
  return 0;
}

u8 SpScriptDefinition::IsAnimated() const {
  return 1;
}

// Load the base definition before its script envelope. Text retains every country's labels/counts and compiles
// all sections, joining backslash continuations; BIN uses the original runtime country-selection loader.
u8 SpScriptDefinition::Load(RsStream *source) {
  u8 result = SpDefinition::Load(source);
  ScString line;
  GetTextLine(source, &line);
  if (line.Strlen() >= 3 && !memcmp(line.Data(), "BIN", 3))
    return SerialReadBinary(source);
  u32 sections = 1, count = 0;
  bool named = line.Strlen() >= 16 && !memcmp(line.Data(), "ScriptCountries:", 16);
  if (named && sscanf(line.ToChar(), "ScriptCountries:%" SCNu32, &sections) != 1)
    return result;
  char country[1024] = {};
  for (u32 section = 0; section < sections; ++section) {
    if (named) {
      GetTextLine(source, &line);
      sscanf(line.ToChar(), "ScriptCountry:%1023s", country);
      *countries.Append() = new ScString(country);
      GetTextLine(source, &line);
    } else {
      *countries.Append() = new ScString("def");
    }
    int parsed = sscanf(line.ToChar(), "Scripts: %" SCNu32, &count);
    *script_counts.Append() = count;
    if (parsed != 1)
      return result;
    for (u32 index = 0; (i32)(u16)index < (i32)count; ++index) {
      ScString text;
      for (;;) {
        ScString part;
        GetTextLine(source, &part);
        text.Append(part.Data(), part.Strlen());
        u32 length = text.Strlen();
        if (length < 2 || text.Data()[length - 2] != '\\')
          break;
        text.Erase(length - 2, 2);
      }
      CompileScript(&text);
    }
  }
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "DSprites: %" SCNu32, &count) == 1) {
    for (u32 index = 0; (i32)(u16)index < (i32)count; ++index) {
      GetTextLine(source, &line);
      ParseChild(&line);
    }
    result = 1;
  }
  return result;
}

// Draw a color frame through its raster interface, using the second image as an alpha mask only for zero flags.
// Fallback truncates flags to a WORD; scaled tint does likewise, while unscaled tint preserves the full word.
u8 SpScriptDefinition::DrawPair(
    GxBuffer *target, const ScRect *rect, u16 frame, u32 flags, const ScRect *clip, u16 mask) {
  if (!mask)
    return frame_table[frame].Draw(target, rect, clip, (u16)flags);
  GxBuffer *color = frame_table[frame].image->buffer, *alpha = frame_table[mask].image->buffer;
  void *object = nullptr;
  if (!color->QueryInterface(IID_GxImageBuilder, &object))
    return frame_table[frame].Draw(target, rect, clip, (u16)flags);
  auto raster = static_cast<GxImageBuilder *>(object);
  if (frame_table[frame].scale) {
    const ScRect *source = color->GetBufferArea();
    if (flags)
      return raster->SpanBufferHighlightBltStretch(target, (u16)flags, *(source), *(rect), clip);
    return raster->SpanBufferAlphaBltStretch(target, alpha, *(source), *(rect), clip);
  }
  ScRect source = {0, 0, (i32)((u32)rect->right - (u32)rect->left), (i32)((u32)rect->bottom - (u32)rect->top)};
  if (flags)
    return raster->SpanBufferHighlightBlt(target, flags, source, ScPoint{(rect)->left, (rect)->top}, clip);
  return raster->SpanBufferAlphaBlt(target, alpha, source, ScPoint{(rect)->left, (rect)->top}, clip);
}

SpScriptState::SpScriptState() : position{INT32_MAX, INT32_MAX, INT32_MAX} {
}

// Destroy the runtime arrays in their original reverse member order; borrowed script records remain owned elsewhere.
SpScriptState::~SpScriptState() = default;

// Create runtime arrays while sharing immutable tracks and their original mutable calendar objects with the definition.
// Each child receives its own descriptor and a null instance pointer, preserving source zoom/rotation traversal order.
u8 SpScriptDefinition::CreateState(SpScriptState **out) {
  *out = new SpScriptState;
  (*out)->position = {0, 0, 0};
  (*out)->zoom = -1;
  (*out)->rotation = -1;
  (*out)->clock = 0;
  (*out)->base_visible = 1;
  for (u32 group = 0; group < 20; ++group) {
    for (u32 i = 0; i < tracks[group].size; ++i)
      *(*out)->tracks[group].Append() = tracks[group].data[i];
    for (u32 i = 0; i < calendars[group].size; ++i)
      *(*out)->calendars[group].Append() = calendars[group].data[i];
    for (u32 i = 0; i < children[group].size; ++i) {
      SpScriptChild *child = new SpScriptChild;
      child->resource = children[group].data[i]->resource;
      child->instance = nullptr;
      child->position = children[group].data[i]->position;
      *(*out)->children[group].Append() = child;
    }
  }
  return 1;
}

// Create the resource's stored instance class through the original GZCOM registry, then bind this definition on
// success.
u8 SpScriptDefinition::CreateInstance(SpSprite **out) {
  if (!SpGetCom()->GetClassObject(instance_type, IID_SpSprite, reinterpret_cast<void **>(out)))
    return 0;
  (*out)->SetDefinition(this);
  return 1;
}

// Start through the real secondary animation interface, preparing all already-demanded views before publishing runtime.
// Existing runtime is left untouched; the fourth input remains present even though this implementation does not use it.
u8 SpScriptSprite::Start(i32 x, i32 z, i32 altitude, u32) {
  if (state)
    return 1;
  if (!definition)
    return 0;
  SpScriptState *value;
  static_cast<SpScriptDefinition *>(definition)->CreateState(&value);
  value->position = {x, z, altitude};
  value->zoom = -1;
  value->rotation = -1;
  SpArtLock.Lock();
  for (u32 zoom = 0; zoom < 5; ++zoom) {
    for (u32 rotation = 0; rotation < 4; ++rotation) {
      u32 mask = 1u << (rotation + 4u * zoom);
      if ((demanded & mask) && !(prepared & mask)) {
        Prepare(value, (i32)zoom, rotation);
        prepared |= mask;
      }
    }
  }
  state = value;
  SpArtLock.Unlock();
  return 1;
}

// Release prepared operation art, remove/release selected-view children, then destroy owned descriptors and runtime.
// Base/default demand bits remain live for the separate ReleaseArt calls; stopping an inactive instance returns one.
u8 SpScriptSprite::Stop() {
  if (state) {
    SpArtLock.Lock();
    for (u32 zoom = 0; zoom < 5; ++zoom) {
      for (u32 rotation = 0; rotation < 4; ++rotation) {
        u32 mask = 1u << (rotation + 4u * zoom);
        if (prepared & mask) {
          ReleasePrepared(state, (i32)zoom, rotation);
          prepared &= ~mask;
        }
      }
    }
    SpArtLock.Unlock();
    if ((u32)state->zoom <= 4u) {
      SpManagerView *manager = SpGetSpriteManager();
      u32 group = (u32)state->rotation + 4u * (u32)state->zoom;
      for (u32 i = 0; i < state->children[group].size; ++i) {
        SpScriptChild *child = state->children[group].data[i];
        if (child->instance) {
          CtCoord position = {(i32)((u32)child->position.x + (u32)state->position.x),
              (i32)((u32)child->position.z + (u32)state->position.z),
              (i32)((u32)child->position.altitude + (u32)state->position.altitude)};
          if (manager)
            manager->RemoveUnalignedSpriteInst(child->instance, position);
          child->instance->Release();
          child->instance = nullptr;
        }
      }
    }
    for (u32 group = 0; group < 20; ++group)
      for (u32 i = 0; i < state->children[group].size; ++i)
        delete state->children[group].data[i];
    delete state;
    state = nullptr;
  }
  return 1;
}

SpScriptSprite::SpScriptSprite() : state(nullptr), demanded(0), prepared(0) {
}

SpScriptSprite::~SpScriptSprite() {
  if (state)
    Stop();
}

bool SpScriptSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpAnimation, SpAnimation);
  return SpInstance::QueryInterface(iid, out);
}

u32 SpScriptSprite::AddRef() {
  return SpInstance::AddRef();
}

u32 SpScriptSprite::Release() {
  return SpInstance::Release();
}

bool SpScriptSprite::SetDefinition(SpDefinition *value) {
  return SpInstance::SetDefinition(value);
}

// Update an active animation's absolute coordinates without creating runtime or touching view/child membership.
void SpScriptSprite::SetPosition(i32 x, i32 z, i32 altitude, u32) {
  if (state) {
    state->position.x = x;
    state->position.z = z;
    state->position.altitude = altitude;
  }
}

ScLock SpArtLock;

// Reset one view's per-instance counters/defaults, then demand every nonzero D/DA frame in track/operation order.
// Track and calendar objects remain shared; preparing a view does not change the currently selected view.
u8 SpScriptSprite::Prepare(SpScriptState *value, i32 zoom, u32 rotation) {
  u32 group = rotation + 4u * (u32)zoom;
  value->pc[group].Clear();
  value->frames[group].Clear();
  value->wake[group].Clear();
  value->offset_x[group].Clear();
  value->offset_y[group].Clear();
  SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(definition);
  ScArray<u32> *frames = &resource->frames[group];
  ScArray<i32> *offset_x = &resource->offset_x[group], *offset_y = &resource->offset_y[group];
  ScArray<SpScriptTrack *> *tracks = &value->tracks[group];
  u32 count = tracks->size;
  for (u32 i = 0; i < count; ++i) {
    *value->pc[group].Append() = 0;
    *value->wake[group].Append() = 0;
    *value->frames[group].Append() = frames->data[i];
    *value->offset_x[group].Append() = offset_x->data[i];
    *value->offset_y[group].Append() = offset_y->data[i];
  }
  for (u32 i = 0; i < tracks->size; ++i) {
    SpScriptTrack *track = tracks->data[i];
    for (u32 j = 0; j < track->operations.size; ++j) {
      SpScriptOp *operation = track->operations.data[j];
      if (operation->code == 1 && operation->argument)
        definition->DemandFrame((u16)operation->argument);
      operation = track->operations.data[j];
      if (operation->code == 2 && operation->argument) {
        definition->DemandFrame((u16)operation->argument);
        operation = track->operations.data[j];
        definition->DemandFrame((u16)(operation->argument + 1u));
      }
    }
  }
  return 1;
}

// Release the operation-frame references acquired by preparation, preserving track order and DA's color/mask pair.
u8 SpScriptSprite::ReleasePrepared(SpScriptState *value, i32 zoom, u32 rotation) {
  u32 group = rotation + 4u * (u32)zoom;
  ScArray<SpScriptTrack *> *tracks = &value->tracks[group];
  for (u32 i = 0; i < tracks->size; ++i) {
    SpScriptTrack *track = tracks->data[i];
    for (u32 j = 0; j < track->operations.size; ++j) {
      SpScriptOp *operation = track->operations.data[j];
      if (operation->code == 1 && operation->argument)
        definition->ReleaseFrame((u16)operation->argument);
      operation = track->operations.data[j];
      if (operation->code == 2 && operation->argument) {
        definition->ReleaseFrame((u16)operation->argument);
        operation = track->operations.data[j];
        definition->ReleaseFrame((u16)(operation->argument + 1u));
      }
    }
  }
  return 1;
}

// Demand the rotated base/default frames once per view, then prepare operation art when runtime data exists.
// The shared sprite-art lock covers all reference calls and mask writes; the unused third argument is retained.
u8 SpScriptSprite::DemandArt(i32 zoom, i32 rotation, bool) {
  SpArtLock.Lock();
  u32 group = (u32)rotation + 4u * (u32)zoom, mask = 1u << (group & 31u);
  if (!(demanded & mask)) {
    definition->DemandFrame((u16)GetFrameIndex(zoom, (u8)rotation));
    ScArray<u32> *frames = &static_cast<SpScriptDefinition *>(definition)->frames[group];
    for (u32 i = 0; i < frames->size; ++i)
      definition->DemandFrame((u16)frames->data[i]);
    demanded |= mask;
  }
  if (state && !(prepared & mask)) {
    Prepare(state, zoom, (u32)rotation);
    prepared |= mask;
  }
  SpArtLock.Unlock();
  return 1;
}

// Release base/default and prepared-operation frame references independently, clearing their original per-view masks.
// Preserve the source's full DWORD one result after releasing the shared art lock.
i32 SpScriptSprite::ReleaseArt(i32 zoom, i32 rotation) {
  SpArtLock.Lock();
  u32 group = (u32)rotation + 4u * (u32)zoom, mask = 1u << (group & 31u);
  if (demanded & mask) {
    definition->ReleaseFrame((u16)GetFrameIndex(zoom, (u8)rotation));
    ScArray<u32> *frames = &static_cast<SpScriptDefinition *>(definition)->frames[group];
    for (u32 i = 0; i < frames->size; ++i)
      definition->ReleaseFrame((u16)frames->data[i]);
    demanded &= ~mask;
  }
  if (prepared & mask) {
    ReleasePrepared(state, zoom, (u32)rotation);
    prepared &= ~mask;
  }
  SpArtLock.Unlock();
  return 1;
}

// Use the rotated base width unless the runtime hides it, in which case the first runtime frame supplies the width.
i32 SpScriptSprite::GetW(i32 zoom, i32 rotation) const {
  if (!state || state->base_visible)
    return definition->GetWidth((u16)GetFrameIndex(zoom, (u8)rotation));
  return definition->GetWidth((u16)state->frames[(u32)rotation + 4u * (u32)zoom].data[0]);
}

// Preserve the height lookup's raw signed result and the same base-versus-first-track selection as the original.
i32 SpScriptSprite::GetH(i32 zoom, i32 rotation) const {
  if (!state || state->base_visible)
    return definition->GetHeight((u16)GetFrameIndex(zoom, (u8)rotation));
  return definition->GetHeight((u16)state->frames[(u32)rotation + 4u * (u32)zoom].data[0]);
}

// Form positioned bounds from the visible base plus successful track rectangles; hidden bases use only the first frame.
// Definition defaults include frame zero, while live runtime bounds skip zero words before querying the resource.
u8 SpScriptSprite::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  u32 group = (u32)rotation + 4u * (u32)zoom;
  if (state && !state->base_visible) {
    definition->GetDrawRect((u16)state->frames[group].data[0], out, x, y);
    return 1;
  }
  definition->GetDrawRect((u16)GetFrameIndex(zoom, (u8)rotation), out, x, y);
  SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(definition);
  ScArray<u32> *frames = state ? &state->frames[group] : &resource->frames[group];
  ScArray<i32> *offset_x = state ? &state->offset_x[group] : &resource->offset_x[group];
  ScArray<i32> *offset_y = state ? &state->offset_y[group] : &resource->offset_y[group];
  u32 count = frames->size;
  for (u32 i = 0; i < count; ++i) {
    if (!state || frames->data[i]) {
      ScRect rect;
      i32 left = (i32)((u32)x + (u32)offset_x->data[i]);
      i32 top = (i32)((u32)y + (u32)offset_y->data[i]);
      if (definition->GetDrawRect((u16)frames->data[i], &rect, left, top))
        out->Union(out, &rect);
    }
  }
  return 1;
}

// Query the base registration, then raise its top edge for each active track using the original offset-only rule.
// Hidden-base instances return the first runtime frame's raw registration result without applying track offsets.
u8 SpScriptSprite::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  u32 group = (u32)rotation + 4u * (u32)zoom;
  if (!state)
    return definition->GetRegistration((u16)GetFrameIndex(zoom, (u8)rotation), out);
  if (!state->base_visible)
    return definition->GetRegistration((u16)state->frames[group].data[0], out);
  definition->GetRegistration((u16)GetFrameIndex(zoom, (u8)rotation), out);
  u32 count = state->frames[group].size;
  for (u32 i = 0; i < count; ++i) {
    if (state->frames[group].data[i]) {
      ScRect discarded;
      definition->GetRegistration((u16)state->frames[group].data[i], &discarded);
      i32 top = (i32)(0u - (u32)state->offset_y[group].data[i]);
      if (top > out->top)
        out->top = top;
    }
  }
  return 1;
}

// Draw the base and either definition defaults or live runtime frames through the real resource renderer.
// DA runtime words select the adjacent color frame and original mask frame; child instances draw through the map.
u8 SpScriptSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  ScRect rect;
  definition->GetDrawRect((u16)GetFrameIndex(zoom, (u8)rotation), &rect, x, y);
  u32 group = (u32)rotation + 4u * (u32)zoom;
  if (state) {
    if (state->base_visible)
      definition->DrawFrame(target, &rect, (u16)GetFrameIndex(zoom, (u8)rotation), flags & kSpInstanceEffectMask, clip);
    u32 count = state->frames[group].size;
    for (u32 i = 0; i < count; ++i) {
      u32 frame = state->frames[group].data[i];
      if (frame) {
        i32 left = (i32)((u32)x + (u32)state->offset_x[group].data[i]);
        i32 top = (i32)((u32)y + (u32)state->offset_y[group].data[i]);
        definition->GetDrawRect((u16)frame, &rect, left, top);
        frame = state->frames[group].data[i];
        if (frame & kSpScriptFramePairFlag)
          static_cast<SpScriptDefinition *>(definition)
              ->DrawPair(target, &rect, (u16)(frame + 1u), flags & kSpInstanceEffectMask, clip, (u16)frame);
        else
          static_cast<SpScriptDefinition *>(definition)
              ->DrawPair(target, &rect, (u16)frame, flags & kSpInstanceEffectMask, clip, 0);
      }
    }
  } else {
    definition->DrawFrame(target, &rect, (u16)GetFrameIndex(zoom, (u8)rotation), flags & kSpInstanceEffectMask, clip);
    SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(definition);
    ScArray<u32> *frames = &resource->frames[group];
    ScArray<i32> *offset_x = &resource->offset_x[group], *offset_y = &resource->offset_y[group];
    for (u32 i = 0; i < frames->size; ++i) {
      i32 left = (i32)((u32)x + (u32)offset_x->data[i]);
      i32 top = (i32)((u32)y + (u32)offset_y->data[i]);
      definition->GetDrawRect((u16)frames->data[i], &rect, left, top);
      definition->DrawFrame(target, &rect, (u16)frames->data[i], flags & kSpInstanceEffectMask, clip);
    }
  }
  return 1;
}

// Test the base first, then every live/default frame relative to its registration and script offsets.
// Base visibility and zero frame words do not suppress these original hit queries; stop at the first success.
bool SpScriptSprite::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const {
  if (definition->HitTest((u16)GetFrameIndex(zoom, (u8)rotation), x, y))
    return true;
  ScRect origin;
  definition->GetRegistration((u16)GetFrameIndex(zoom, (u8)rotation), &origin);
  u32 group = (u32)rotation + 4u * (u32)zoom;
  SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(definition);
  ScArray<u32> *frames = state ? &state->frames[group] : &resource->frames[group];
  ScArray<i32> *offset_x = state ? &state->offset_x[group] : &resource->offset_x[group];
  ScArray<i32> *offset_y = state ? &state->offset_y[group] : &resource->offset_y[group];
  for (u32 i = 0; i < frames->size; ++i) {
    i32 left = (i32)((u32)x - (u32)origin.left - (u32)offset_x->data[i]);
    i32 top = (i32)((u32)y - (u32)offset_y->data[i] - (u32)origin.top);
    if (definition->HitTest((u16)frames->data[i], left, top))
      return true;
  }
  return false;
}

// Render an existing destination rectangle, retaining this entry's different DA color/mask ordering and zero-frame calls.
// Without runtime data, default overlays are positioned from base registration rather than the destination rectangle.
u8 SpScriptSprite::Draw(GxBuffer *target, const ScRect &destination, i32 zoom, i32 rotation, ScRect *clip) const {
  ScRect rect = destination;
  u32 group = (u32)rotation + 4u * (u32)zoom;
  if (state) {
    if (state->base_visible)
      definition->DrawFrame(target, &rect, (u16)GetFrameIndex(zoom, (u8)rotation), flags & kSpInstanceEffectMask, clip);
    for (u32 i = 0; i < state->frames[group].size; ++i) {
      u32 frame = state->frames[group].data[i];
      static_cast<SpScriptDefinition *>(definition)
          ->DrawPair(target, &rect, (u16)frame, flags & kSpInstanceEffectMask, clip,
              frame & kSpScriptFramePairFlag ? (u16)(frame + 1u) : 0);
    }
  } else {
    definition->DrawFrame(
        target, &destination, (u16)GetFrameIndex(zoom, (u8)rotation), flags & kSpInstanceEffectMask, clip);
    ScRect origin;
    definition->GetRegistration((u16)GetFrameIndex(zoom, (u8)rotation), &origin);
    SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(definition);
    ScArray<u32> *frames = &resource->frames[group];
    ScArray<i32> *offset_x = &resource->offset_x[group], *offset_y = &resource->offset_y[group];
    for (u32 i = 0; i < frames->size; ++i) {
      i32 left = (i32)((u32)origin.left + (u32)offset_x->data[i]);
      i32 top = (i32)((u32)origin.top + (u32)offset_y->data[i]);
      definition->GetDrawRect((u16)frames->data[i], &rect, left, top);
      definition->DrawFrame(target, &rect, (u16)frames->data[i], flags & kSpInstanceEffectMask, clip);
    }
  }
  return 1;
}

// Return the first matching label operation's index. A missing label retains the original zero result.
u32 SC3_STDCALL SpFindScriptLabel(SpScriptTrack *track, u32 label) {
  for (u32 i = 0; i < track->operations.size; ++i) {
    SpScriptOp *operation = track->operations.data[i];
    if (operation->code == 13 && operation->argument == label)
      return i;
  }
  return 0;
}

// Advance the selected view's real animation tracks, report changed frame bounds and tick resident children in order.
// Preserve signed waits, calendar/label control, live runtime reads and the original instance-address wait staggering.
i32 SpScriptSprite::Tick(u32 delta) {
  if (!state)
    return 0;
  i32 zoom = state->zoom, rotation = state->rotation;
  SpManagerView *manager = SpGetSpriteManager();
  if ((u32)zoom > 4) {
    SpCellMap *map = SpGetCellMap();
    state->zoom = zoom = map->GetZoom();
    state->rotation = rotation = map->GetRotate();
    SelectView(zoom, rotation);
  }
  state->clock += delta >> 10;
  u32 group = (u32)rotation + 4u * (u32)zoom;
  ScArray<u32> *pc = &state->pc[group];
  u32 month, day, year;
  SpGetCity()->GetDate(&month, &day, &year);
  ScRect origin{};
  bool have_origin = false;
  u32 count = pc->size;
  for (u32 i = 0; i < count; ++i) {
    ScArray<SpScriptTrack *> *tracks = &state->tracks[group];
    SpScriptCalendar *calendar = state->calendars[group].data[i];
    u32 *wake = &state->wake[group].data[i];
    if ((i32)state->clock < (i32)*wake)
      break;
    if (!*wake)
      *wake = state->clock;
    if (!have_origin) {
      have_origin = true;
      GetRegistration(zoom, rotation, &origin);
    }
    SpScriptOp *operation;
    for (;;) {
      SpScriptTrack *track = tracks->data[i];
      if ((i32)pc->data[i] >= (i32)track->operations.size)
        pc->data[i] = 0;
      operation = track->operations.data[pc->data[i]];
      if (operation->code != 13)
        break;
      ++pc->data[i];
    }
    switch (operation->code) {
    case 1:
    case 2: {
      state->frames[group].data[i] = operation->argument | (operation->code == 2 ? kSpScriptFramePairFlag : 0);
      ScRect rectangle;
      definition->GetFrameRect((u16)state->frames[group].data[i], &rectangle);
      i32 x = operation->code == 2 ? (i32)(operation->extra >> 16) : (i16)(operation->extra >> 16);
      state->offset_x[group].data[i] = x;
      i32 y = operation->code == 2 ? (i32)(u16)operation->extra : (i16)operation->extra;
      state->offset_y[group].data[i] = y;
      u32 translate_y = (u32)origin.top + (u32)y, translate_x = (u32)origin.left + (u32)x;
      rectangle.top = (i32)((u32)rectangle.top + translate_y);
      rectangle.bottom = (i32)((u32)rectangle.bottom + translate_y);
      rectangle.left = (i32)((u32)rectangle.left + translate_x);
      rectangle.right = (i32)((u32)rectangle.right + translate_x);
      if (state->base_visible) {
        if (manager)
          manager->ChangeCellAlignedSpriteInst(state->position, 2, &rectangle);
      } else if (manager)
        manager->ChangeUnalignedSpriteInst(this, state->position, 2, &rectangle);
      ++pc->data[i];
      break;
    }
    case 3:
      state->wake[group].data[i] = operation->argument + state->clock +
          (u32)(((uptr)this >> 4) & 3u) * ((operation->argument >> 5) + (operation->argument >> 6));
      ++pc->data[i];
      break;
    case 4:
      state->wake[group].data[i] = operation->argument + state->clock + (u32)ScRand() % operation->extra;
      ++pc->data[i];
      break;
    case 5:
      pc->data[i] = operation->argument;
      break;
    case 6:
      pc->data[i] += (u32)ScRand() % operation->argument + 1u;
      break;
    case 11:
    case 12: {
      calendar->active = 1;
      calendar->label = operation->code != 11;
      calendar->target = operation->extra;
      u32 date = operation->argument;
      calendar->end[1] = date % 100;
      calendar->end[0] = date / 100 % 100;
      calendar->begin[1] = date / 10000 % 100;
      calendar->begin[0] = date / kSpCalendarBeginMonthDivisor;
      ++pc->data[i];
      break;
    }
    case 14:
      pc->data[i] = SpFindScriptLabel(tracks->data[i], calendar->target);
      break;
    default:
      break;
    }
    if (calendar && calendar->active == 1 &&
        (month < calendar->begin[0] || (month == calendar->begin[0] && day < calendar->begin[1]) ||
            month > calendar->end[0] || (month == calendar->end[0] && day >= calendar->end[1]))) {
      pc->data[i] = calendar->label ? SpFindScriptLabel(tracks->data[i], calendar->target) : calendar->target;
      calendar->active = 0;
    }
  }
  for (u32 i = 0; i < state->children[group].size; ++i) {
    SpSprite *child = state->children[group].data[i]->instance;
    if (child)
      child->Tick(delta);
  }
  return 0;
}

// Select the new view before removing its resident children, then recreate that view's descriptors through real resources.
// Old-view children are not purged; resource and instance failures retain the original checks and unchecked dependencies.
void SpScriptSprite::SelectView(i32 zoom, i32 rotation) {
  if (!state)
    return;
  state->zoom = zoom;
  state->rotation = rotation;
  SpManagerView *manager = SpGetSpriteManager();
  u32 group = (u32)rotation + 4u * (u32)zoom;
  for (u32 i = 0; i < state->children[group].size; ++i) {
    SpScriptChild *child = state->children[group].data[i];
    if (child->instance) {
      CtCoord position = {(i32)((u32)child->position.x + (u32)state->position.x),
          (i32)((u32)child->position.z + (u32)state->position.z),
          (i32)((u32)child->position.altitude + (u32)state->position.altitude)};
      if (manager)
        manager->RemoveUnalignedSpriteInst(child->instance, position);
      child->instance->Release();
      child->instance = nullptr;
    }
  }
  SpScriptState *runtime;
  for (u32 i = 0; i < state->children[group].size; ++i) {
    SpScriptChild *child = state->children[group].data[i];
    RsKey key = {0x6301, 0x6400, child->resource};
    void *object = nullptr;
    if (SpGetResourceManager()->GetResource(key, IID_SpDefView, &object)) {
      SpScriptDefinition *resource = static_cast<SpScriptDefinition *>(object);
      resource->CreateInstance(&child->instance);
      if (resource->CreateState(&runtime)) {
        runtime->base_visible = 0;
        runtime->zoom = zoom;
        runtime->rotation = rotation;
        SpScriptSprite *instance = static_cast<SpScriptSprite *>(child->instance);
        instance->SetState(runtime);
        instance->SetPosition((i32)((u32)child->position.x + (u32)state->position.x),
            (i32)((u32)child->position.z + (u32)state->position.z),
            (i32)((u32)child->position.altitude + (u32)state->position.altitude), 0);
      }
      if (resource)
        resource->Release();
      manager = SpGetSpriteManager();
      if (manager)
        manager->InsertUnalignedSpriteInst(child->instance, runtime->position, 2, 2);
    }
  }
}

// Destroy the previous runtime's child descriptors and array storage, then adopt the supplied state pointer.
u8 SpScriptSprite::SetState(SpScriptState *value) {
  if (state) {
    for (u32 group = 0; group < 20; ++group)
      for (u32 i = 0; i < state->children[group].size; ++i)
        delete state->children[group].data[i];
    delete state;
  }
  state = value;
  return 1;
}

// Apply the original signed WORD slope offset after the ordinary definition registration query.
// Unsigned shifting preserves the defined game-zoom calculation without shifting a negative signed integer.
static u8 SpGetSlopedRegistration(const SpInstance *sprite, i32 zoom, i32 rotation, ScRect *out, i32 offset,
    i32 first_rotation, i32 second_rotation) {
  u8 result = sprite->SpInstance::GetRegistration(zoom, rotation, out);
  u32 facing = sprite->flags >> kSpInstanceRotationShift;
  if (facing == u32(first_rotation) || facing == u32(second_rotation)) {
    i32 shift = i16(u16(u32(offset) << (u32(zoom) & 31)));
    out->top += shift;
    out->bottom -= shift;
  }
  return result;
}

// Merge a successful terrain registration into the instance's already computed registration rectangle.
// The terrain supplies horizontal width; negative own anchors extend the corresponding vertical extent.
static u8 SpMergeCsmRegistration(SpSprite *terrain, i32 zoom, i32 rotation, ScRect *out, u8 result) {
  if (!result || !terrain)
    return result;
  ScRect bounds{};
  result = terrain->GetRegistration(zoom, rotation, &bounds);
  if (!result)
    return result;
  out->left = 0;
  out->right = bounds.right;
  if (out->bottom < 0)
    out->bottom = bounds.bottom - out->bottom;
  else if (bounds.bottom > out->bottom)
    out->bottom = bounds.bottom;
  if (out->top < 0)
    out->top = bounds.top - out->top;
  else if (bounds.top > out->top)
    out->top = bounds.top;
  return result;
}

// These four original specialization entries are160D7,1617D,1622D and16271 for offsets-1,+1,-2,+2.
// The raw definition result is preserved even when the selected rotation adjusts the output rectangle.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedSprite<Offset, FirstRotation, SecondRotation>::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  return SpGetSlopedRegistration(this, zoom, rotation, out, Offset, FirstRotation, SecondRotation);
}

// Original162B6 positions the sloped registration around a point in top/bottom/left/right store order.
// Zero seeds only avoid reading originally unwritten locals when a failed definition query leaves them unavailable.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedSprite<Offset, FirstRotation, SecondRotation>::GetAreaFromPt(
    i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  ScRect bounds{};
  u8 result = GetRegistration(zoom, rotation, &bounds);
  out->top = y - bounds.top;
  out->bottom = y + bounds.bottom;
  out->left = x - bounds.left;
  out->right = x + bounds.right;
  return result;
}

// Original16117/161C0 stretch the frame rectangle only in the two slope-facing rotations.
// The other rotations use the ordinary point draw and its actual definition result.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedSprite<Offset, FirstRotation, SecondRotation>::Draw(
    GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  u32 facing = flags >> kSpInstanceRotationShift;
  if (facing != FirstRotation && facing != SecondRotation)
    return SpInstance::Draw(target, x, y, zoom, rotation, clip);
  ScRect bounds{};
  GetAreaFromPt(zoom, rotation, &bounds, x, y);
  return SpInstance::Draw(target, bounds, zoom, rotation, clip);
}

SpCsmSprite::SpCsmSprite() : terrain(nullptr) {
}

SpCsmSprite::~SpCsmSprite() {
  ScRelease(&terrain);
}

i32 SpCsmSprite::GetW(i32 zoom, i32 rotation) const {
  return terrain ? terrain->GetW(zoom, rotation) : SpInstance::GetW(zoom, rotation);
}

// Derive height from the receiver's virtual registration so terrain and own artwork contribute together.
// Failed queries with unwritten extents are outside the original defined-read contract.
i32 SpCsmSprite::GetH(i32 zoom, i32 rotation) const {
  ScRect bounds{};
  GetRegistration(zoom, rotation, &bounds);
  return bounds.top + bounds.bottom;
}

// Query own artwork first, then expand a successful registration with the current retained terrain sprite.
// A terrain failure preserves the own output rectangle and returns that raw failure byte.
u8 SpCsmSprite::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  u8 result = SpInstance::GetRegistration(zoom, rotation, out);
  if (!result)
    return result;
  return SpMergeCsmRegistration(terrain, zoom, rotation, out, result);
}

// Convert a successful virtual registration to absolute bounds in the caller's output storage.
// Failed queries retain exactly the stores performed by the registration call.
u8 SpCsmSprite::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  u8 result = GetRegistration(zoom, rotation, out);
  if (result) {
    out->right += x;
    out->left = x - out->left;
    i32 top = y - out->top;
    out->bottom += y;
    out->top = top;
  }
  return result;
}

u8 SpCsmSprite::SetEffect(u32 effect) {
  u8 result = terrain ? terrain->SetEffect(effect) : 1;
  return result | SpInstance::SetEffect(effect);
}

// Test own artwork first, then translate the point when the retained terrain extends above it.
// Terrain is queried again through its live member after the virtual registration callbacks.
bool SpCsmSprite::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const {
  if (SpInstance::IsPixelOpaque(x, y, zoom, rotation))
    return true;
  if (!terrain)
    return false;
  ScRect bounds{}, ground{};
  GetRegistration(zoom, rotation, &bounds);
  terrain->GetRegistration(zoom, rotation, &ground);
  if (bounds.top > ground.top)
    y += ground.top - bounds.top;
  return terrain->IsPixelOpaque(x, y, zoom, rotation);
}

// Demand retained terrain first and stop if that request fails.
// Only a successful terrain demand reaches the ordinary instance's own frame demand.
u8 SpCsmSprite::DemandArt(i32 zoom, i32 rotation, bool force) {
  if (terrain) {
    u8 result = terrain->DemandArt(zoom, rotation, force);
    if (!result)
      return result;
  }
  return SpInstance::DemandArt(zoom, rotation, force);
}

// Release retained terrain's artwork before releasing the ordinary instance frame.
// The complete return word comes from the own definition's frame release.
i32 SpCsmSprite::ReleaseArt(i32 zoom, i32 rotation) {
  if (terrain)
    terrain->ReleaseArt(zoom, rotation);
  return SpInstance::ReleaseArt(zoom, rotation);
}

// Use a direct definition rectangle draw when no terrain is attached.
// Otherwise translate through virtual registration and invoke the receiver's point-draw implementation.
u8 SpCsmSprite::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!terrain)
    return SpInstance::Draw(target, rect, zoom, rotation, clip);
  ScRect bounds{};
  GetRegistration(zoom, rotation, &bounds);
  return Draw(target, bounds.left + rect.left, bounds.top + rect.top, zoom, rotation, clip);
}

// Draw retained terrain at the supplied point before drawing the ordinary instance over it.
// The terrain draw's return is discarded; the own frame supplies the raw result byte.
u8 SpCsmSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  if (terrain)
    terrain->Draw(target, x, y, zoom, rotation, clip);
  return SpInstance::Draw(target, x, y, zoom, rotation, clip);
}

// Retain a replacement terrain before releasing the current member, then publish the replacement.
void SpCsmSprite::SetNext(SpSprite *value) {
  if (value)
    value->AddRef();
  if (terrain)
    terrain->Release();
  terrain = value;
}

// Original factories15FC3/16008/1604D/16092 construct the instance and clear its sole extra pointer field.
// The four specialization vtables add no other state or constructor side effects.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::SpSlopedCsmSprite() : terrain(nullptr) {
}

// Original1691F/16984/169E9/16A4E retire the retained terrain before ordinary instance destruction.
// Reset occurs after the Release callback, matching the non-sloped CSM lifetime.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::~SpSlopedCsmSprite() {
  ScRelease(&terrain);
}

// Original166F3 returns retained terrain width when present, otherwise the ordinary definition width.
// The shared body is folded across the four original specializations.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
i32 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::GetW(i32 zoom, i32 rotation) const {
  return terrain ? terrain->GetW(zoom, rotation) : SpInstance::GetW(zoom, rotation);
}

// Original16439 derives combined height from the receiver's virtual registration.
// Zero local seeds exclude the original failed-query unwritten reads without inventing an error result.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
i32 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::GetH(i32 zoom, i32 rotation) const {
  ScRect bounds{};
  GetRegistration(zoom, rotation, &bounds);
  return bounds.top + bounds.bottom;
}

// Original162F9/164CA/16613/1671B merge retained terrain only after applying their corresponding slope query.
// Raw registration failure bytes and the original partial output writes are preserved.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::GetRegistration(
    i32 zoom, i32 rotation, ScRect *out) const {
  u8 result = SpGetSlopedRegistration(this, zoom, rotation, out, Offset, FirstRotation, SecondRotation);
  if (!result)
    return result;
  return SpMergeCsmRegistration(terrain, zoom, rotation, out, result);
}

// Original16369 translates a successful registration in the caller's output rectangle.
// Its failure branch leaves any registration-produced stores intact.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::GetAreaFromPt(
    i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  u8 result = GetRegistration(zoom, rotation, out);
  if (result) {
    out->right += x;
    out->left = x - out->left;
    i32 top = y - out->top;
    out->bottom += y;
    out->top = top;
  }
  return result;
}

// Original1639F applies retained terrain's effect before updating the ordinary instance flags.
// Keep the original bitwise combination rather than normalizing raw result bytes.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::SetEffect(u32 effect) {
  u8 result = terrain ? terrain->SetEffect(effect) : 1;
  return result | SpInstance::SetEffect(effect);
}

// Original1645A tests the own frame, then uses combined and terrain anchors to translate a missed point.
// The final hit test reuses the live retained terrain member after the preceding callbacks.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
bool SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::IsPixelOpaque(
    i32 x, i32 y, i32 zoom, i32 rotation) const {
  if (SpInstance::IsPixelOpaque(x, y, zoom, rotation))
    return true;
  if (!terrain)
    return false;
  ScRect bounds{}, ground{};
  GetRegistration(zoom, rotation, &bounds);
  terrain->GetRegistration(zoom, rotation, &ground);
  if (bounds.top > ground.top)
    y += ground.top - bounds.top;
  return terrain->IsPixelOpaque(x, y, zoom, rotation);
}

// Original1678B demands retained terrain first and propagates its failed result byte directly.
// Own artwork is demanded only after that dependency succeeds or when no terrain is attached.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::DemandArt(i32 zoom, i32 rotation, bool force) {
  if (terrain) {
    u8 result = terrain->DemandArt(zoom, rotation, force);
    if (!result)
      return result;
  }
  return SpInstance::DemandArt(zoom, rotation, force);
}

// Original167BF releases terrain artwork before selecting and releasing the own frame.
// Preserve the actual definition release result rather than the discarded terrain result.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
i32 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::ReleaseArt(i32 zoom, i32 rotation) {
  if (terrain)
    terrain->ReleaseArt(zoom, rotation);
  return SpInstance::ReleaseArt(zoom, rotation);
}

// Original165AA always translates through virtual registration and dispatches the point-draw overload.
// Unlike the non-sloped CSM path, it has no terrain-null shortcut.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::Draw(
    GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  ScRect bounds{};
  GetRegistration(zoom, rotation, &bounds);
  return Draw(target, bounds.left + rect.left, bounds.top + rect.top, zoom, rotation, clip);
}

// Original163C9/1653A/16683/167F7 draw terrain first, then stretch only the own sloped frame around the point.
// The own rectangle deliberately uses the slope-only registration rather than the combined terrain bounds.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
u8 SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::Draw(
    GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  if (terrain)
    terrain->Draw(target, x, y, zoom, rotation, clip);
  ScRect bounds{};
  SpGetSlopedRegistration(this, zoom, rotation, &bounds, Offset, FirstRotation, SecondRotation);
  bounds.right += x;
  bounds.bottom += y;
  bounds.left = x - bounds.left;
  bounds.top = y - bounds.top;
  return SpInstance::Draw(target, bounds, zoom, rotation, clip);
}

// Original165EC retains a replacement terrain before releasing the current one and publishing the new pointer.
// The inherited chain getter is not used to expose this retained rendering dependency.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
void SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::SetNext(SpSprite *value) {
  if (value)
    value->AddRef();
  if (terrain)
    terrain->Release();
  terrain = value;
}

// Original folded slot96 reports no external chained sprite for every CSM slope specialization.
// The retained terrain remains private to registration, drawing and artwork ownership.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation>
SpSprite *SpSlopedCsmSprite<Offset, FirstRotation, SecondRotation>::GetNext() {
  return nullptr;
}

template class SpSlopedSprite<-1, 0, 1>;
template class SpSlopedSprite<1, 2, 3>;
template class SpSlopedSprite<-2, 0, 1>;
template class SpSlopedSprite<2, 2, 3>;
template class SpSlopedCsmSprite<-1, 0, 1>;
template class SpSlopedCsmSprite<1, 2, 3>;
template class SpSlopedCsmSprite<-2, 0, 1>;
template class SpSlopedCsmSprite<2, 2, 3>;

// Construct class59D with the downward one-unit slope specialization and no extra reference.
// The instance constructor supplies its ordinary packed flags and empty definition.
ScUnknown *SpCreateSlopedDown() {
  return new SpSlopedSprite<-1, 0, 1>;
}

// Construct class59E with the upward one-unit slope specialization and no extra reference.
// Its packed rotation selectors are the opposite pair from class59D.
ScUnknown *SpCreateSlopedUp() {
  return new SpSlopedSprite<1, 2, 3>;
}

// Construct class59F with the downward two-unit slope specialization and no extra reference.
// It retains the same ordinary instance state as the one-unit variant.
ScUnknown *SpCreateSlopedDown2() {
  return new SpSlopedSprite<-2, 0, 1>;
}

// Construct class5A0 with the upward two-unit slope specialization and no extra reference.
// Only its registration offset and rotation pair distinguish this original class.
ScUnknown *SpCreateSlopedUp2() {
  return new SpSlopedSprite<2, 2, 3>;
}

// Construct class672 with its actual empty retained-terrain member.
// The director later obtains the requested interface from the ordinary sprite identity.
ScUnknown *SpCreateCsmSprite() {
  return new SpCsmSprite;
}

// Construct class674's downward one-unit CSM slope and return its unretained primary identity.
// The actual terrain dependency is attached later through SetNext.
ScUnknown *SpCreateSlopedCsmDown() {
  return new SpSlopedCsmSprite<-1, 0, 1>;
}

// Construct class675's upward one-unit CSM slope and return its unretained primary identity.
// Its sole additional pointer starts empty, as in the installed factory.
ScUnknown *SpCreateSlopedCsmUp() {
  return new SpSlopedCsmSprite<1, 2, 3>;
}

// Construct class676's downward two-unit CSM slope and return its unretained primary identity.
// Terrain and definition ownership follow their separate original lifetimes.
ScUnknown *SpCreateSlopedCsmDown2() {
  return new SpSlopedCsmSprite<-2, 0, 1>;
}

// Construct class677's upward two-unit CSM slope and return its unretained primary identity.
// No caller-visible reference is acquired by construction itself.
ScUnknown *SpCreateSlopedCsmUp2() {
  return new SpSlopedCsmSprite<2, 2, 3>;
}

SpDefinition::SpDefinition()
    : frame_table(nullptr), frame_count(0), animation_frames(0), animation_set_count(0), zoom_set_count(0),
      registration_rectangles(0), layer_flags(0), geometry_flags(0) {
}

SpDefinition::~SpDefinition() {
  delete[] frame_table;
  frame_table = nullptr;
}

bool SpDefinition::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpDefView, SpDefView);
  return RsResource::QueryInterface(iid, out);
}

u32 SpDefinition::AddRef() {
  return RsResource::AddRef();
}

u32 SpDefinition::Release() {
  return RsResource::Release();
}

bool SpDefinition::HitTest(u16 frame, i32 x, i32 y) {
  return frame_table[frame].HitTest(x, y);
}

i32 SpDefinition::GetWidth(u16 frame) {
  return frame_table[frame].GetWidth();
}

i32 SpDefinition::GetHeight(u16 frame) {
  return frame_table[frame].GetHeight();
}

u8 SpDefinition::GetFrameRect(u16 frame, ScRect *out) {
  return frame_table[frame].GetFrameRect(out);
}

u8 SpDefinition::GetRegistrationPoint(u16 frame, i32 *x, i32 *y) const {
  SpFrame *selected = &frame_table[frame];
  if (!selected->image)
    return 0;
  *x = selected->Scale(selected->image->left);
  *y = selected->Scale(selected->image->top);
  return 1;
}

u8 SpDefinition::GetRegistration(u16 frame, ScRect *out) {
  return frame_table[frame].GetRegistration(out);
}

u8 SpDefinition::GetDrawRect(u16 frame, ScRect *out, i32 x, i32 y) {
  return frame_table[frame].GetDrawRect(out, x, y);
}

// Create the base definition's one-, two- or three-part sprite chain according to its layer flags.
u8 SpDefinition::CreateInstance(SpSprite **out) {
  SpSprite *second = nullptr, *third = nullptr;
  if (!SpGetCom()->GetClassObject(instance_type, IID_SpSprite, reinterpret_cast<void **>(out)))
    return 0;
  (*out)->SetDefinition(this);
  if (layer_flags & 1) {
    if (!SpGetCom()->GetClassObject(instance_type, IID_SpSprite, reinterpret_cast<void **>(&second))) {
      (*out)->Release();
      return 0;
    }
    second->SetDefinition(this);
    second->SetLayer(1);
    (*out)->SetNext(second);
    if ((*out)->GetNext() != second) {
      second->Release();
      (*out)->Release();
      return 0;
    }
    if (layer_flags & 2) {
      if (!SpGetCom()->GetClassObject(instance_type, IID_SpSprite, reinterpret_cast<void **>(&third))) {
        second->Release();
        (*out)->Release();
        return 0;
      }
      third->SetDefinition(this);
      third->SetLayer(2);
      second->SetNext(third);
      if (second->GetNext() != third) {
        third->Release();
        second->Release();
        (*out)->Release();
        return 0;
      }
    }
  }
  return 1;
}

// Rebind the existing chain's third, second and first sprites to this definition, without replacing ownership.
u8 SpDefinition::RebindInstance(SpSprite **instance) {
  if (!instance || !*instance)
    return 0;
  SpSprite *second = (*instance)->GetNext();
  if (second) {
    SpSprite *third = second->GetNext();
    if (third)
      third->SetDefinition(this);
    second->SetDefinition(this);
  }
  (*instance)->SetDefinition(this);
  return 1;
}

u8 SpDefinition::DestroyInstance(SpSprite **instance) {
  if (!instance || !*instance)
    return 0;
  SpSprite *current = *instance, *next;
  while ((next = current->GetNext())) {
    current->Release();
    current = next;
  }
  current->Release();
  *instance = nullptr;
  return 1;
}

u8 SpDefinition::SetGeomFlag(u32 mask) {
  geometry_flags |= mask;
  return 1;
}

u8 SpFrame::DemandArt() {
  return SpGetImageCache()->PrepareImage(image->resource, image->group, nullptr);
}

u32 SpFrame::ReleaseArt() {
  return SpGetImageCache()->UnprepareImage(image->resource, image->group);
}

u8 SpDefinition::DemandFrame(u16 frame) {
  return frame_table[frame].DemandArt();
}

u32 SpDefinition::ReleaseFrame(u16 frame) {
  return frame_table[frame].ReleaseArt();
}

// Read the binary frame header and records after the resource's BIN marker, preserving each completed field write.
// Existing tables are retained; a missing cache record ends loading without replacing the last stream return byte.
u8 SpDefinition::SerialReadBinary(RsStream *source) {
  SpImageCache *cache = SpGetImageCache();
  if (!cache || !cache->IsInitialized())
    return 0;
  i32 version;
  u8 result = source->ReadI32(&version);
  result = result && source->ReadU32(&instance_type) && source->ReadU8(&zoom_count) && source->ReadU8(&rotation_count) &&
      source->ReadU8(&zoom_set_count) && source->ReadU8(&animation_set_count) && source->ReadU8(&animation_frames) &&
      source->ReadU8(&registration_rectangles) && source->ReadU32(&layer_flags) && source->ReadU16(&frame_count);
  if (result && !frame_table) {
    frame_table = new SpFrame[frame_count];
    u32 group = 0, resource = 0;
    for (u32 index = 0; index < frame_count; ++index) {
      u16 stored_index;
      SpFrame *frame = &frame_table[index];
      result = source->ReadU16(&stored_index);
      if (!result)
        break;
      result = source->ReadU32(&group);
      if (!result)
        break;
      result = source->ReadU32(&resource);
      if (!result)
        break;
      result = source->ReadI8(&frame->scale);
      if (!result)
        break;
      result = source->ReadI16(&frame->offset_x);
      if (!result)
        break;
      result = source->ReadI16(&frame->offset_y);
      if (!result)
        break;
      frame->flags = 0;
      frame->image = cache->GetImageInfo(resource, group);
      if (!frame->image)
        break;
    }
  }
  return result;
}

// Clear the concrete String, skip LF bytes and read until CR through the real record's byte-block method.
// A complete line includes its explicit NUL byte; EOF keeps the partial bytes and returns zero.
u8 SpDefinition::GetTextLine(RsStream *source, ScString *out) {
  out->Erase(0, out->ScString::Strlen());
  u8 value;
  for (;;) {
    if (!source->ReadBlock(&value, 1))
      return 0;
    if (value == 10)
      continue;
    if (value == 13) {
      value = 0;
      out->Append(&value, 1);
      return 1;
    }
    out->Append(&value, 1);
  }
}

// Load a BIN definition or the original ordered text fields, retaining completed writes on a failed scan.
u8 SpDefinition::Load(RsStream *source) {
  SpImageCache *cache = SpGetImageCache();
  if (!cache || !cache->IsInitialized())
    return 0;
  ScString line;
  GetTextLine(source, &line);
  if (line.Strlen() >= 3 && !memcmp(line.Data(), "BIN", 3))
    return SpDefinition::SerialReadBinary(source);
  while (line.ToChar()[0] == ';')
    GetTextLine(source, &line);
  u32 version, value, group = 0, resource = 0;
  i32 zoom;
  if (sscanf(line.ToChar(), "Version: %" SCNu32, &version) != 1)
    return 0;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "SprInstCLSID: %" SCNu32, &instance_type) != 1)
    return 0;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "ZoomCount: %" SCNd32, &zoom) != 1)
    return 0;
  zoom_count = (u8)zoom;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "RotCount: %" SCNu32, &value) != 1)
    return 0;
  rotation_count = (u8)value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "ZoomSetCount: %" SCNu32, &value) != 1)
    return 0;
  zoom_set_count = (u8)value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "AnimSetCount: %" SCNu32, &value) != 1)
    return 0;
  animation_set_count = (u8)value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "AnimFrames: %" SCNu32, &value) != 1)
    return 0;
  animation_frames = (u8)value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "RegRects: %" SCNu32, &value) != 1)
    return 0;
  registration_rectangles = (u8)value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "LayerFlags: %" SCNu32 "d", &value) != 1)
    return 0;
  layer_flags = value;
  GetTextLine(source, &line);
  if (sscanf(line.ToChar(), "Frames: %" SCNu32, &value) != 1)
    return 0;
  frame_count = (u16)value;
  if (frame_table)
    return 0;
  frame_table = new SpFrame[frame_count];
  for (u32 index = 0; index < frame_count; ++index) {
    i32 stored_index, scale, x, y;
    GetTextLine(source, &line);
    if (sscanf(line.ToChar(), "Frame: %" SCNd32 ", %08" SCNx32 ", %08" SCNx32 ", %" SCNd32 ", %" SCNd32 ", %" SCNd32,
            &stored_index, &group, &resource, &scale, &x, &y) != 6)
      return 0;
    frame_table[index].scale = (i8)scale;
    frame_table[index].flags = 0;
    frame_table[index].offset_x = (i16)x;
    frame_table[index].offset_y = (i16)y;
    SpImage *image = cache->GetImageInfo(resource, group);
    frame_table[index].image = image;
    if (!frame_table[index].image)
      return 0;
  }
  return 1;
}

SpFrame::SpFrame() : flags(0), scale(0), offset_x(0), offset_y(0), image(nullptr) {
}

SpFrame::~SpFrame() {
  image = nullptr;
}

// Apply the signed scale convention: zero leaves values unchanged, positive multiplies and negative divides.
// Negative scaling preserves the original truncation toward zero before negating the quotient.
i32 SpFrame::Scale(i32 value) {
  if (!scale)
    return value;
  return scale > 0 ? value * scale : -(value / scale);
}

i32 SpFrame::GetWidth() {
  return image ? Scale((i32)image->left + image->right) : 0;
}

i32 SpFrame::GetHeight() {
  return image ? Scale((i32)image->top + image->bottom) : 0;
}

// Write a zero-origin extent using each whole anchor sum before scaling. Missing images leave output untouched.
u8 SpFrame::GetFrameRect(ScRect *out) {
  if (!image)
    return 0;
  out->top = 0;
  out->left = 0;
  out->right = Scale((i32)image->left + image->right);
  out->bottom = Scale((i32)image->top + image->bottom);
  return 1;
}

// Return each anchor scaled separately, preserving the distinction from scaling their summed extent.
// An unloaded frame returns zero without reading scale or accessing the output pointer.
u8 SpFrame::GetRegistration(ScRect *out) {
  if (!image)
    return 0;
  out->left = Scale(image->left);
  out->top = Scale(image->top);
  out->right = Scale(image->right);
  out->bottom = Scale(image->bottom);
  return 1;
}

// Position scaled anchors around the supplied screen coordinate. Missing image metadata leaves output untouched.
u8 SpFrame::GetDrawRect(ScRect *out, i32 x, i32 y) {
  if (!image)
    return 0;
  out->left = x - Scale(image->left);
  out->top = y - Scale(image->top);
  out->right = x + Scale(image->right);
  out->bottom = y + Scale(image->bottom);
  return 1;
}

// Convert nonnegative displayed coordinates into the unscaled image and test its signed anchor extents.
// The original two mutable outputs and their update order are retained, including an aliased point pair.
bool SpFrame::MapPoint(i32 *x, i32 *y) {
  if (*x < 0 || *y < 0 || !image)
    return false;
  if (scale > 0) {
    *x /= scale;
    *y /= scale;
  } else if (scale < 0) {
    *x = -(*x * scale);
    *y = -(*y * scale);
  }
  return *x < (i32)image->left + image->right && *y < (i32)image->top + image->bottom;
}

// Map the displayed point, lock the actual image buffer and reject its transparent pixels.
// Only successful locks are unlocked; the transparency result is retained across that call.
bool SpFrame::HitTest(i32 x, i32 y) {
  if (!MapPoint(&x, &y))
    return false;
  GxBuffer *buffer = image->buffer;
  if (!buffer || !buffer->Lock(GxLockFlags::GetSetPixel))
    return false;
  bool result = !buffer->IsPixelTransparent((u32)x, (u32)y);
  buffer->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

// Draw cached pixels through the real buffer and image interfaces, selecting scaling, packed tint or opacity.
// Ordinary paths retain the queried reference; opacity and unsupported high-word effects release it and return zero.
u8 SpFrame::Draw(GxBuffer *target, const ScRect *rect, const ScRect *clip, u32 effect) {
  if (!image || !image->buffer)
    return 0;
  GxBuffer *buffer = image->buffer;
  void *object = nullptr;
  if (!buffer->QueryInterface(IID_GxImageBuilder, &object))
    return 0;
  GxImageBuilder *raster = static_cast<GxImageBuilder *>(object);
  if (!scale) {
    if (!(effect & kSpFrameHighWordEffectMask)) {
      ScRect source = {0, 0, rect->right - rect->left, rect->bottom - rect->top};
      if (effect)
        return raster->SpanBufferHighlightBlt(target, effect, source, ScPoint{(rect)->left, (rect)->top}, clip);
      return buffer->BltSrc(target, source, *(rect), clip);
    }
    if (effect & kSpDrawOpacity) {
      ScRect source = {0, 0, rect->right - rect->left, rect->bottom - rect->top};
      f32 opacity = (f32)((f64)(u8)effect * (f64)0.00392f);
      raster->SpanBufferAlphaBltConstAlpha(target, opacity, source, ScPoint{(rect)->left, (rect)->top}, clip);
    }
  } else {
    if (!(effect & kSpFrameHighWordEffectMask)) {
      const ScRect *source = buffer->GetBufferArea();
      if (effect)
        return raster->SpanBufferHighlightBltStretch(target, effect, *(source), *(rect), clip);
      return buffer->BltStretchSrc(target, *(source), *(rect), clip);
    }
    if (effect & kSpDrawOpacity) {
      const ScRect *source = buffer->GetBufferArea();
      f32 opacity = (f32)((f64)(u8)effect * (f64)0.00392f);
      raster->SpanBufferAlphaBltConstAlphaStretch(target, opacity, *(source), *(rect), clip);
    }
  }
  if (raster)
    raster->Release();
  return 0;
}

u8 SpDefinition::DrawFrame(GxBuffer *target, const ScRect *rect, u16 frame, u32 effect, const ScRect *clip) {
  return frame_table[frame].Draw(target, rect, clip, effect);
}

SpInstance::SpInstance() : flags(0), definition(nullptr) {
}

// Release the bound resource during base destruction, after derived animation cleanup has completed.
SpInstance::~SpInstance() {
  if (definition)
    definition->Release();
  definition = nullptr;
}

// Return the primary sprite identity for Unknown or7100, retaining it only on success and clearing rejected outputs.
bool SpInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpSprite);
  HANDLE_QUERY_INTERFACE(IID_SpSprite, SpSprite);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the original thirteen-bit reference field without changing low drawing flags or high rotation bits.
u32 SpInstance::AddRef() {
  u32 count = ((flags >> kSpInstanceReferenceShift) + 1u) & kSpInstanceReferenceValueMask;
  flags = (flags & ~kSpInstanceReferenceMask) | (count << kSpInstanceReferenceShift);
  return count;
}

// Destroy the dynamic instance at its last reference; other counts wrap in the original thirteen-bit field.
u32 SpInstance::Release() {
  u32 count = (flags >> kSpInstanceReferenceShift) & kSpInstanceReferenceValueMask;
  if (count == 1) {
    delete this;
    return 0;
  }
  count = (count - 1u) & kSpInstanceReferenceValueMask;
  flags = (flags & ~kSpInstanceReferenceMask) | (count << kSpInstanceReferenceShift);
  return count;
}

// Drop the previous definition before retaining its replacement, including the original self-binding order.
bool SpInstance::SetDefinition(SpDefinition *value) {
  if (definition)
    definition->Release();
  definition = value;
  if (definition)
    definition->AddRef();
  return definition != nullptr;
}

void SpInstance::GetRotationOffset(u8 *out) const {
  *out = (u8)(flags >> kSpInstanceRotationShift);
}

void SpInstance::SetRotationOffset(u8 rotation) {
  flags = (flags & kSpInstanceWithoutRotation) | (u32(rotation) << kSpInstanceRotationShift);
}

void SpInstance::SetLayer(u8) {
}

void SpInstance::SetNext(SpSprite *) {
}

u8 SpInstance::SetGeomFlag(u32 mask) {
  return definition ? definition->SetGeomFlag(mask) : 0;
}

// This implementation base has no chained sprite; specialized chain instances override the operation.
SpSprite *SpInstance::GetNext() {
  return nullptr;
}

u32 SpInstance::GetFrameIndex(i32 zoom, u8 rotation) const {
  return (((u32)rotation + (flags >> kSpInstanceRotationShift)) & 3u) + 4u * (u32)zoom;
}

int SpDefinition::HasGeomFlag(u32 mask) {
  return (geometry_flags & mask) == mask;
}

u8 SpDefinition::ClearGeomFlag(u32 mask) {
  geometry_flags &= ~mask;
  return 1;
}

u32 SpDefinition::GetGeomFlag() {
  return geometry_flags;
}

// Draw a frame's zero-origin extent translated by its signed offsets and the supplied point.
// Missing image metadata returns zero before reading offsets or invoking the raster dispatcher.
u8 SpFrame::DrawOffset(GxBuffer *target, i32 x, i32 y, const ScRect *clip, u32 effect) {
  ScRect rectangle;
  if (!GetFrameRect(&rectangle))
    return 0;
  i32 dx = x + offset_x, dy = y + offset_y;
  rectangle.left += dx;
  rectangle.right += dx;
  rectangle.top += dy;
  rectangle.bottom += dy;
  return Draw(target, &rectangle, clip, effect);
}

u8 SpDefinition::DrawOffset(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) {
  return frame_table[frame].DrawOffset(target, x, y, clip, effect);
}

// Place the frame's separately scaled registration anchors around the point, then draw that exact rectangle.
u8 SpDefinition::DrawAt(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) {
  SpFrame *image = &frame_table[frame];
  ScRect rectangle;
  if (!image->GetDrawRect(&rectangle, x, y))
    return 0;
  return image->Draw(target, &rectangle, clip, effect);
}

i32 SpInstance::GetW(i32 zoom, i32 rotation) const {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->GetWidth(frame);
}

i32 SpInstance::GetH(i32 zoom, i32 rotation) const {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->GetHeight(frame);
}

u8 SpInstance::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->GetRegistration(frame, out);
}

u8 SpInstance::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->GetDrawRect(frame, out, x, y);
}

bool SpInstance::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->HitTest(frame, x, y);
}

u8 SpInstance::DemandArt(i32 zoom, i32 rotation, bool) {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->DemandFrame(frame);
}

i32 SpInstance::ReleaseArt(i32 zoom, i32 rotation) {
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return (i32)definition->ReleaseFrame(frame);
}

// Draw the selected static frame into an existing rectangle with only the low17 packed effect bits.
u8 SpInstance::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  u32 effect = flags & kSpInstanceEffectMask;
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->DrawFrame(target, &rect, frame, effect, clip);
}

// Draw the selected static frame at a point, capturing the original effect bits before virtual view selection.
u8 SpInstance::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  u32 effect = flags & kSpInstanceEffectMask;
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->DrawAt(target, x, y, frame, effect, clip);
}

i32 SpInstance::Tick(u32) {
  return 0;
}

u8 SpInstance::HasGeomFlag(u32 mask) {
  return definition ? (u8)definition->HasGeomFlag(mask) : 0;
}

u8 SpInstance::ClearGeomFlag(u32 mask) {
  return definition ? definition->ClearGeomFlag(mask) : 0;
}

u32 SpInstance::GetGeomFlag() {
  return definition ? definition->GetGeomFlag() : 0;
}

u32 SpInstance::GetLayerFlags() const {
  return definition->GetLayerFlags();
}

u8 SpInstance::SetEffect(u32 effect) {
  flags ^= (effect ^ flags) & kSpInstanceEffectMask;
  return 1;
}

u16 SpDefinition::SprAttGetTotalFrameCount() const {
  return frame_count;
}

u8 SpDefinition::GetAnimationSetCount() const {
  return animation_set_count;
}

u8 SpDefinition::GetZoomCount() const {
  return zoom_count;
}

u8 SpDefinition::GetRotationCount() const {
  return rotation_count;
}

u8 SpDefinition::GetAnimationFrameCount() const {
  return animation_frames;
}

u8 SpDefinition::GetZoomSetCount() const {
  return zoom_set_count;
}

u8 SpDefinition::GetRegistrationRectCount() const {
  return registration_rectangles;
}

u8 SpDefinition::GetResourceKey(RsKey *out) const {
  *out = key;
  return 1;
}

u8 SpDefinition::IsAnimated() const {
  return 0;
}

// Write the binary definition header and each frame in source order, stopping stream calls after the first refusal.
u8 SpDefinition::Write(RsStream *target) {
  bool result = target->WriteU8('B') && target->WriteU8('I') && target->WriteU8('N') && target->WriteU8('\r') &&
      target->WriteU32(1) && target->WriteU32(instance_type) && target->WriteU8(zoom_count) &&
      target->WriteU8(rotation_count) && target->WriteU8(zoom_set_count) && target->WriteU8(animation_set_count) &&
      target->WriteU8(animation_frames) && target->WriteU8(registration_rectangles) &&
      target->WriteU32(layer_flags) && target->WriteU16(frame_count);
  for (u32 index = 0; u16(index) < frame_count; ++index) {
    if (result) {
      u16 frame = u16(index);
      result = target->WriteI16(i16(index)) && target->WriteU32(frame_table[frame].image->group) &&
          target->WriteU32(frame_table[frame].image->resource) && target->WriteI8(frame_table[frame].scale) &&
          target->WriteI16(frame_table[frame].offset_x) && target->WriteI16(frame_table[frame].offset_y);
    }
  }
  return result;
}

SpLayerSprite::SpLayerSprite() : next(nullptr), frame_offset(0), adjusted(0) {
}

SpLayerSprite::~SpLayerSprite() {
  ScRelease(&next);
}

// Change this layer's packed rotation first, then propagate the original byte argument to its live child.
void SpLayerSprite::SetRotationOffset(u8 rotation) {
  SpInstance::SetRotationOffset(rotation);
  if (next)
    next->SetRotationOffset(rotation);
}

u8 SpLayerSprite::SetEffect(u32 effect) {
  u8 result = next ? next->SetEffect(effect) : 0;
  return result | SpInstance::SetEffect(effect);
}

void SpLayerSprite::SetLayer(u8 layer) {
  frame_offset = (u8)(layer * definition->GetAnimationSetCount());
}

// Release and clear the previous child before retaining the new link. A null input simply disconnects the chain.
void SpLayerSprite::SetNext(SpSprite *value) {
  ScRelease(&next);
  if (value) {
    next = value;
    value->AddRef();
  }
}

// Borrow the next layer without retaining it. This leaf is folded with other original offset12 getters.
SpSprite *SpLayerSprite::GetNext() {
  return next;
}

// Enable anchor adjustment before forwarding a geometry mask containing bit29 to the actual definition.
u8 SpLayerSprite::SetGeomFlag(u32 mask) {
  if (mask & kSpLayerAnchorAdjustmentGeometry)
    adjusted = 1;
  return SpInstance::SetGeomFlag(mask);
}

u8 SpLayerSprite::ClearGeomFlag(u32 mask) {
  if (mask & kSpLayerAnchorAdjustmentGeometry)
    adjusted = 0;
  return SpInstance::ClearGeomFlag(mask);
}

// Demand this layer before its child, always passing the original zero third word to both art operations.
// Either zero-byte result makes the combined result false; child acquisition still runs after a parent refusal.
u8 SpLayerSprite::DemandArt(i32 zoom, i32 rotation, bool) {
  bool failed = SpInstance::DemandArt(zoom, rotation, 0) == 0;
  if (next)
    failed |= next->DemandArt(zoom, rotation, 0) == 0;
  return !failed;
}

// Release the child's artwork first and this layer second.
i32 SpLayerSprite::ReleaseArt(i32 zoom, i32 rotation) {
  if (next)
    next->ReleaseArt(zoom, rotation);
  SpInstance::ReleaseArt(zoom, rotation);
  return 1;
}

u8 SpLayerSprite::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  static const i32 shift_x[20] = {0, 4, 8, 4, 0, 8, 16, 8, 0, 16, 32, 16, 0, 32, 64, 32, 0, 64, 128, 64};
  static const i32 shift_y[20] = {0, 2, 1, -1, 0, 4, 1, -3, -1, 7, 0, -8, -1, 15, 0, -16, -1, 31, 0, -32};
  SpInstance::GetRegistration(zoom, rotation, out);
  if (adjusted) {
    i32 index = rotation + 4 * zoom;
    out->left += shift_x[index];
    out->top += shift_y[index];
    out->right -= shift_x[index];
    out->bottom -= shift_y[index];
  }
  return 1;
}

u8 SpLayerSprite::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  if (!GetRegistration(zoom, rotation, out))
    return 0;
  if (adjusted) {
    out->right += x;
    out->left = x - out->left;
    i32 top = y - out->top;
    out->bottom += y;
    out->top = top;
  }
  return 1;
}

// Draw adjusted layers through their explicit rectangle, otherwise use the definition's anchor-based point path.
// Capture effect bits after the optional bounds callback and before virtual frame selection, as in the original.
u8 SpLayerSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  if (adjusted) {
    ScRect rectangle;
    GetAreaFromPt(zoom, rotation, &rectangle, x, y);
    u32 effect = flags & kSpInstanceEffectMask;
    u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
    return definition->DrawFrame(target, &rectangle, frame, effect, clip);
  }
  u32 effect = flags & kSpInstanceEffectMask;
  u16 frame = (u16)GetFrameIndex(zoom, (u8)rotation);
  return definition->DrawAt(target, x, y, frame, effect, clip);
}

u32 SpLayerSprite::GetFrameIndex(i32 zoom, u8 rotation) const {
  return frame_offset + (((u32)rotation + (flags >> kSpInstanceRotationShift)) & 3u) + 4u * (u32)zoom;
}

u32 SpDefinition::GetLayerFlags() const {
  return layer_flags;
}

static bool SpFallbackInitialized;
static ScRect SpFallbackSpans[5][5];
u32 SpFallbackColor;

// Publish the shared span-initialization guard before querying metadata, then reset this fallback's per-zoom state.
// The two original inputs are signed integers; only the footprint's low byte is stored and the second input is unused.
u8 SpGenericBuilding::Init(i32 size, i32) {
  if (!SpFallbackInitialized) {
    SpFallbackInitialized = true;
    StaticInit();
  }
  footprint = (u8)size;
  for (u32 i = 0; i < 5; ++i) {
    demands[i] = 0;
    buffers[i] = nullptr;
  }
  return 1;
}

// Read the original five-by-five image-anchor table, doubling only the displayed highest-zoom row.
void SpGenericBuilding::StaticInit() {
  SpImageCache *cache = SpGetImageCache();
  for (i32 zoom = 0; zoom < 5; ++zoom)
    for (u32 size = 0; size < 5; ++size) {
      SpImage *image = cache->GetImageInfo(GetSpriteKey(zoom, size, kSpGenericBuildingImageBase), 20);
      ScRect *span = &SpFallbackSpans[zoom][size];
      span->left = image->left;
      span->top = image->top;
      span->right = image->right;
      span->bottom = image->bottom;
      if (zoom == 4) {
        span->left *= 2;
        span->top *= 2;
        span->right *= 2;
        span->bottom *= 2;
      }
    }
}

// Share zoom-three art with zoom four and combine the original zoom/footprint fields with the supplied base key.
// Unsigned key arithmetic retains the installed DWORD result without an extra resource lookup.
u32 SpGenericBuilding::GetSpriteKey(i32 zoom, u32 size, u32 base) {
  if (zoom == 4)
    zoom = 3;
  return base + 4u * ((u32)zoom + (size << 14));
}

i32 SpGenericBuilding::GetW(i32 zoom, i32) const {
  return SpFallbackSpans[zoom][footprint].right;
}

i32 SpGenericBuilding::GetH(i32 zoom, i32) const {
  return SpFallbackSpans[zoom][footprint].right;
}

// Copy the four live registration spans in original edge order and return the ordinary success byte.
u8 SpGenericBuilding::GetRegistration(i32 zoom, i32, ScRect *out) const {
  const ScRect *span = &SpFallbackSpans[zoom][footprint];
  out->left = span->left;
  out->top = span->top;
  out->right = span->right;
  out->bottom = span->bottom;
  return 1;
}

// Position the cached spans around the supplied point, preserving copy and edge-write order.
// The rectangle is the original mutable output rather than a bundled result.
u8 SpGenericBuilding::GetAreaFromPt(i32 zoom, i32, ScRect *out, i32 x, i32 y) const {
  const ScRect *span = &SpFallbackSpans[zoom][footprint];
  out->left = span->left;
  out->top = span->top;
  out->right = span->right;
  out->bottom = span->bottom;
  i32 left = (i32)((u32)x - (u32)out->left);
  out->right = (i32)((u32)out->right + (u32)x);
  out->left = left;
  out->bottom = (i32)((u32)out->bottom + (u32)y);
  out->top = (i32)((u32)y - (u32)out->top);
  return 1;
}

// Halve highest-zoom coordinates before checking the borrowed buffer and testing its actual color-key pixel.
// Preserve color lookup before pixel read and unlock only after a successful lock.
bool SpGenericBuilding::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32) const {
  if (zoom == 4) {
    x /= 2;
    y /= 2;
  }
  GxBuffer *buffer = buffers[zoom];
  if (!buffer || x < 0 || y < 0 || x >= (i32)buffer->Width() || y >= (i32)buffer->Height() ||
      !buffer->Lock(GxLockFlags::GetSetPixel))
    return false;
  u32 key = buffer->GetBltFlags()->pixel;
  bool result = buffer->GetPixel((u32)x, (u32)y) != key;
  buffer->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

// Acquire cache art only for the first demand at a zoom, then increment that zoom's count even for a false byte reply.
// The original defined path supplies image metadata; buffers remain borrowed from that cache record.
u8 SpGenericBuilding::DemandArt(i32 zoom, i32, bool) {
  u8 result = 1;
  if (!demands[zoom]) {
    SpImageCache *cache = SpGetImageCache();
    SpImage *image = nullptr;
    result = cache->PrepareImage(GetSpriteKey(zoom, footprint, kSpGenericBuildingImageBase), 20, &image);
    buffers[zoom] = image->buffer;
  }
  ++demands[zoom];
  return result;
}

// Drop the cache reference only at the last demand, clearing the borrowed buffer before that callback.
// Other releases return the original constant one and every call decrements the per-zoom DWORD count.
i32 SpGenericBuilding::ReleaseArt(i32 zoom, i32) {
  u32 result = 1;
  if (demands[zoom] == 1) {
    SpImageCache *cache = SpGetImageCache();
    buffers[zoom] = nullptr;
    result = cache->UnprepareImage(GetSpriteKey(zoom, footprint, kSpGenericBuildingImageBase), 20);
  }
  --demands[zoom];
  return (i32)result;
}

// Query the real raster identity, then draw cached art directly or scaled at zoom four using the original tint rule.
// Preserve the installed acquired-reference behavior: these successful drawing branches do not release that identity.
u8 SpGenericBuilding::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32, ScRect *clip) const {
  GxBuffer *buffer = buffers[zoom];
  if (!buffer)
    return 0;
  void *object = nullptr;
  if (!buffer->QueryInterface(IID_GxImageBuilder, &object))
    return 0;
  GxImageBuilder *raster = static_cast<GxImageBuilder *>(object);
  if (zoom == 4) {
    if (flags & kSpInstanceEffectMask) {
      const ScRect *source = buffer->GetBufferArea();
      return raster->SpanBufferHighlightBltStretch(target, SpFallbackColor, *(source), rect, clip);
    }
    const ScRect *source = buffer->GetBufferArea();
    return buffer->BltStretchSrc(target, *(source), rect, clip);
  }
  ScRect source{0, 0, (i32)((u32)rect.right - (u32)rect.left), (i32)((u32)rect.bottom - (u32)rect.top)};
  if (flags & kSpInstanceEffectMask)
    return raster->SpanBufferHighlightBlt(target, SpFallbackColor, source, ScPoint{(rect).left, (rect).top}, clip);
  return buffer->BltSrc(target, source, rect, clip);
}

// Compute the original positioned rectangle through virtual dispatch, then forward it to the rectangle draw overload.
// A refused rectangle query returns zero without a raster or buffer call.
u8 SpGenericBuilding::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  ScRect rectangle;
  if (!GetAreaFromPt(zoom, rotation, &rectangle, x, y))
    return 0;
  return Draw(target, rectangle, zoom, rotation, clip);
}

u32 SpGenericBuilding::GetLayerFlags() const {
  return 0;
}

SpTerrainSprite::SpTerrainSprite(SpSprite *value) : references(0), terrain(value), next(nullptr) {
  value->AddRef();
}

// Publish only the common and sprite identities, acquiring their reference through virtual dispatch. Rejected
// interfaces clear the output.
bool SpTerrainSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpTerrainSprite);
  HANDLE_QUERY_INTERFACE(IID_SpTerrainSprite, SpTerrainSprite);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpTerrainSprite::AddRef() {
  return ++references;
}

// Destroy the wrapper at its last reference without decrementing first.
u32 SpTerrainSprite::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

SpTerrainSprite::~SpTerrainSprite() {
  ScRelease(&terrain);
  ScRelease(&next);
}

i32 SpTerrainSprite::GetW(i32 zoom, i32 rotation) const {
  return terrain->GetW(zoom, rotation);
}

i32 SpTerrainSprite::GetH(i32 zoom, i32 rotation) const {
  return terrain->GetH(zoom, rotation);
}

u8 SpTerrainSprite::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  return terrain->GetRegistration(zoom, rotation, out);
}

u8 SpTerrainSprite::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const {
  return terrain->GetAreaFromPt(zoom, rotation, out, x, y);
}

void SpTerrainSprite::GetRotationOffset(u8 *out) const {
  terrain->GetRotationOffset(out);
}

u32 SpTerrainSprite::GetLayerFlags() const {
  return terrain->GetLayerFlags();
}

// Set the terrain rotation first, then forward to the live chain when present.
// Reload the link after the first callback to preserve defined reentrant changes.
void SpTerrainSprite::SetRotationOffset(u8 rotation) {
  terrain->SetRotationOffset(rotation);
  if (next)
    next->SetRotationOffset(rotation);
}

// Apply effects to the chain before the terrain and combine both returned bytes with bitwise OR.
// Preserve non-Boolean low-byte replies rather than normalizing them to true or false.
u8 SpTerrainSprite::SetEffect(u32 effect) {
  u8 result = 0;
  if (next)
    result = next->SetEffect(effect);
  return result | terrain->SetEffect(effect);
}

void SpTerrainSprite::SetLayer(u8 layer) {
  terrain->SetLayer(layer);
}

bool SpTerrainSprite::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const {
  return terrain->IsPixelOpaque(x, y, zoom, rotation);
}

// A failed first demand does not skip the chain demand or roll back either dependency.
u8 SpTerrainSprite::DemandArt(i32 zoom, i32 rotation, bool argument) {
  bool failed = terrain->DemandArt(zoom, rotation, argument) == 0;
  if (next)
    failed |= next->DemandArt(zoom, rotation, argument) == 0;
  return !failed;
}

// Release the optional chain's artwork before releasing the terrain artwork.
// Both dependency replies are ignored and the original wrapper returns the DWORD value one.
i32 SpTerrainSprite::ReleaseArt(i32 zoom, i32 rotation) {
  if (next)
    next->ReleaseArt(zoom, rotation);
  terrain->ReleaseArt(zoom, rotation);
  return 1;
}

u8 SpTerrainSprite::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  return terrain->Draw(target, rect, zoom, rotation, clip);
}

u8 SpTerrainSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  return terrain->Draw(target, x, y, zoom, rotation, clip);
}

i32 SpTerrainSprite::Tick(u32 elapsed) {
  return terrain->Tick(elapsed);
}

bool SpTerrainSprite::SetDefinition(SpDefinition *definition) {
  return terrain->SetDefinition(definition);
}

u8 SpTerrainSprite::HasGeomFlag(u32 mask) {
  return terrain->HasGeomFlag(mask);
}

u8 SpTerrainSprite::SetGeomFlag(u32 mask) {
  return terrain->SetGeomFlag(mask);
}

u8 SpTerrainSprite::ClearGeomFlag(u32 mask) {
  return terrain->ClearGeomFlag(mask);
}

u32 SpTerrainSprite::GetGeomFlag() {
  return terrain->GetGeomFlag();
}

// Release the old link before publishing and acquiring the new one. Empty-to-empty calls have only the original
void SpTerrainSprite::SetNext(SpSprite *value) {
  ScRelease(&next);
  if (value) {
    next = value;
    value->AddRef();
  }
}

// Return the borrowed chain link without AddRef. These getter bytes are folded with other offset-twelve aliases.
SpSprite *SpTerrainSprite::GetNext() {
  return next;
}
