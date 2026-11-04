#ifndef SC3_SPRITES_SPRITE_H
#define SC3_SPRITES_SPRITE_H

#include "city/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "resources/resource.h"
#include "sprites/api.h"

class GxBuffer;
class SpSprite;
struct SpFrame;
class RsStream;
class ScString;

// Actual primary IID6100, separate from the definition's secondary resource identity.
// Ordering follows the installed primary table; unresolved write and animation contracts remain pure virtual.
class SpDefView : public ScUnknown {
public:
  virtual bool HitTest(u16 frame, i32 x, i32 y) = 0;
  virtual i32 GetWidth(u16 frame) = 0;
  virtual i32 GetHeight(u16 frame) = 0;
  virtual u8 GetFrameRect(u16 frame, ScRect *out) = 0;
  virtual u8 GetRegistrationPoint(u16 frame, i32 *x, i32 *y) const = 0;
  virtual u8 GetRegistration(u16 frame, ScRect *out) = 0;
  virtual u8 GetDrawRect(u16 frame, ScRect *out, i32 x, i32 y) = 0;
  virtual u8 GetZoomCount() const = 0;
  virtual u8 GetRotationCount() const = 0;
  virtual u8 GetAnimationFrameCount() const = 0;
  virtual u8 DemandFrame(u16 frame) = 0;
  virtual u32 ReleaseFrame(u16 frame) = 0;
  virtual u8 DrawFrame(GxBuffer *target, const ScRect *rect, u16 frame, u32 effect, const ScRect *clip) = 0;
  virtual u8 DrawAt(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) = 0;
  virtual u8 DrawOffset(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) = 0;
  virtual u8 GetResourceKey(RsKey *out) const = 0;
  virtual u8 Load(RsStream *source) = 0;
  // Source nominal return spellings are unresolved; keep these operations explicit until recovered.
  virtual u8 Write(RsStream *target) = 0;
  virtual u8 IsAnimated() const = 0;
  virtual int HasGeomFlag(u32 mask) = 0;
  virtual u8 SetGeomFlag(u32 mask) = 0;
  virtual u8 ClearGeomFlag(u32 mask) = 0;
  virtual u32 GetGeomFlag() = 0;
  virtual u8 GetZoomSetCount() const = 0;
  virtual u8 GetAnimationSetCount() const = 0;
  virtual u16 SprAttGetTotalFrameCount() const = 0;
  virtual u8 GetRegistrationRectCount() const = 0;
  virtual u32 GetLayerFlags() const = 0;
  virtual u8 CreateInstance(SpSprite **out) = 0;
  virtual u8 RebindInstance(SpSprite **instance) = 0;
  virtual u8 DestroyInstance(SpSprite **instance) = 0;
  virtual u8 SerialReadBinary(RsStream *source) = 0;
};

// The primary sprite-definition interface and secondary resource interface share the resource reference count.
// The definition owns its frame table; cached images referenced by those frames belong to the image manager.
class SpDefinition : public SpDefView, public RsResource {
public:
  SpDefinition();
  ~SpDefinition() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool HitTest(u16 frame, i32 x, i32 y) override;
  virtual i32 GetWidth(u16 frame) override;
  virtual i32 GetHeight(u16 frame) override;
  virtual u8 GetFrameRect(u16 frame, ScRect *out) override;
  u8 GetRegistrationPoint(u16 frame, i32 *x, i32 *y) const override;
  virtual u8 GetRegistration(u16 frame, ScRect *out) override;
  virtual u8 GetDrawRect(u16 frame, ScRect *out, i32 x, i32 y) override;
  virtual u8 DrawFrame(GxBuffer *target, const ScRect *rect, u16 frame, u32 effect, const ScRect *clip) override;
  virtual u8 DrawAt(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) override;
  virtual u8 DrawOffset(GxBuffer *target, i32 x, i32 y, u16 frame, u32 effect, const ScRect *clip) override;
  u8 GetResourceKey(RsKey *out) const override;
  u8 Write(RsStream *target) override;
  u8 IsAnimated() const override;
  // Acquisition defines AL only; release returns the full remaining DWORD count.
  virtual u8 DemandFrame(u16 frame) override;
  virtual u32 ReleaseFrame(u16 frame) override;
  u16 SprAttGetTotalFrameCount() const override;
  u8 SerialReadBinary(RsStream *source) override;
  u8 Load(RsStream *source) override;
  virtual u8 CreateInstance(SpSprite **out) override;
  virtual u8 RebindInstance(SpSprite **instance) override;
  virtual u8 DestroyInstance(SpSprite **instance) override;
  virtual int HasGeomFlag(u32 mask) override;
  virtual u8 SetGeomFlag(u32 mask) override;
  virtual u8 ClearGeomFlag(u32 mask) override;
  virtual u32 GetGeomFlag() override;
  u8 GetAnimationSetCount() const override;
  u8 GetZoomCount() const override;
  u8 GetRotationCount() const override;
  u8 GetAnimationFrameCount() const override;
  u8 GetZoomSetCount() const override;
  u8 GetRegistrationRectCount() const override;
  u32 GetLayerFlags() const override;
  // Linux names the original nonvirtual member; its bool/BYTE result spelling remains unresolved.
  u8 GetTextLine(RsStream *source, ScString *out);
  SpFrame *frame_table;
  u32 instance_type;
  u16 frame_count;
  u8 animation_frames, animation_set_count, zoom_count, rotation_count, zoom_set_count;
  u8 registration_rectangles;
  u32 layer_flags, geometry_flags;
};

class GxBuffer;

// Cached image record: original +4/+8 identify the resource and group; +12 is the loaded buffer.
// The first DWORD is the live image reference count. Native pointers use host width rather than a foreign overlay.
struct SpImage {
  u32 references, resource, group;
  GxBuffer *buffer;
  i16 left, top, right, bottom;
};

struct SpFrame {
  SpFrame();
  ~SpFrame();
  i32 Scale(i32 value);
  i32 GetWidth();
  i32 GetHeight();
  u8 GetFrameRect(ScRect *out);
  u8 GetRegistration(ScRect *out);
  u8 GetDrawRect(ScRect *out, i32 x, i32 y);
  bool MapPoint(i32 *x, i32 *y);
  bool HitTest(i32 x, i32 y);
  u8 Draw(GxBuffer *target, const ScRect *rect, const ScRect *clip, u32 effect);
  u8 DrawOffset(GxBuffer *target, i32 x, i32 y, const ScRect *clip, u32 effect);
  u8 DemandArt();
  u32 ReleaseArt();
  u8 flags;
  i8 scale;
  i16 offset_x, offset_y;
  SpImage *image;
};

// The four original sloped instance specializations change registration and point drawing without adding fields.
// Linux names preserve their signed height offset and two packed rotation selectors as template arguments.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation> class SpSlopedSprite : public SpInstance {
public:
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  using SpInstance::Draw;
};

// Registered class672 adds a retained CSM terrain sprite to the ordinary instance and its own definition.
// This is distinct from the separate terrain-wrapper class constructed with an existing sprite argument.
class SpCsmSprite : public SpInstance {
public:
  SpCsmSprite();
  ~SpCsmSprite() override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 SetEffect(u32 effect) override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  void SetNext(SpSprite *value) override;

protected:
  SpSprite *terrain;
};

// The four CSM slope specializations have the same verified instance prefix and retained terrain field.
// The original intermediate source-base spelling is not established; no relationship to SpCsmSprite is asserted.
template <i32 Offset, i32 FirstRotation, i32 SecondRotation> class SpSlopedCsmSprite : public SpInstance {
public:
  SpSlopedCsmSprite();
  ~SpSlopedCsmSprite() override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 SetEffect(u32 effect) override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool force) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  void SetNext(SpSprite *value) override;
  SpSprite *GetNext() override;

protected:
  SpSprite *terrain;
};

ScUnknown *SpCreateSlopedDown();
ScUnknown *SpCreateSlopedUp();
ScUnknown *SpCreateSlopedDown2();
ScUnknown *SpCreateSlopedUp2();
ScUnknown *SpCreateCsmSprite();
ScUnknown *SpCreateSlopedCsmDown();
ScUnknown *SpCreateSlopedCsmUp();
ScUnknown *SpCreateSlopedCsmDown2();
ScUnknown *SpCreateSlopedCsmUp2();

class ScLock;
class ScStringView;
class ScString;
class RsStream;

struct SpScriptOp {
  u8 code;
  u32 argument, extra;
};

struct SpScriptTrack {
  ScArray<SpScriptOp *> operations;
};

struct SpScriptCalendar {
  u32 begin[2], end[2];
  u8 active, label;
  u32 target;
};

struct SpScriptChild {
  u32 resource;
  SpSprite *instance;
  CtCoord position;
};

// Actual runtime arrays are distinct from the definition's shared tracks/calendars; each view retains its own counters.
struct SpScriptState {
  SpScriptState();
  ~SpScriptState();
  ScArray<SpScriptTrack *> tracks[20];
  ScArray<SpScriptCalendar *> calendars[20];
  ScArray<SpScriptChild *> children[20];
  ScArray<u32> pc[20], frames[20], wake[20];
  ScArray<i32> offset_x[20], offset_y[20];
  i32 zoom, rotation;
  CtCoord position;
  u32 clock;
  u8 base_visible;
};

// Script-definition arrays preserve shared tracks/calendars and per-instance child descriptor copies.
class SpScriptDefinition : public SpDefinition {
public:
  SpScriptDefinition();
  ~SpScriptDefinition() override;
  u8 Load(RsStream *source) override;
  u8 Write(RsStream *target) override;
  u8 IsAnimated() const override;
  u8 CreateInstance(SpSprite **out) override;
  u8 SerialReadBinary(RsStream *source) override;
  virtual u8 CompileScript(const ScStringView *text);
  virtual u8 ParseChild(const ScStringView *text);
  virtual u8 CreateState(SpScriptState **out);
  virtual u8 DrawPair(GxBuffer *target, const ScRect *rect, u16 frame, u32 flags, const ScRect *clip, u16 mask);
  ScArray<u32> frames[20];
  ScArray<i32> offset_x[20], offset_y[20];
  // Text loading retains country labels and script counts; binary runtime loading selects sections directly.
  // String pointers replace only generic inline storage while retaining ownership of the stored Strings.
  ScArray<ScString *> countries;
  ScArray<u32> script_counts;
  ScArray<SpScriptTrack *> tracks[20];
  ScArray<SpScriptCalendar *> calendars[20];
  ScArray<SpScriptChild *> children[20];
};

// Recovered sprite implementation base and secondary animation receiver; resource parsing and module providers remain partial.
class SpScriptSprite : public SpInstance, public SpAnimation {
public:
  SpScriptSprite();
  ~SpScriptSprite() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool SetDefinition(SpDefinition *value) override;
  u8 Start(i32 x, i32 z, i32 altitude, u32 argument) override;
  u8 Stop() override;
  void SetPosition(i32 x, i32 z, i32 altitude, u32 argument) override;
  i32 Tick(u32 delta) override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  u8 SetState(SpScriptState *value);
  u8 Prepare(SpScriptState *value, i32 zoom, u32 rotation);
  u8 ReleasePrepared(SpScriptState *value, i32 zoom, u32 rotation);
  void SelectView(i32 zoom, i32 rotation);
  SpScriptState *state;
  u32 demanded, prepared;
};

u32 SC3_STDCALL SpFindScriptLabel(SpScriptTrack *track, u32 label);
extern ScLock SpArtLock;

#endif
