#ifndef SC3_WINDOW_INPUT_H
#define SC3_WINDOW_INPUT_H

#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"

#ifdef LoadCursor
#undef LoadCursor
#endif

// The common file cursor owns its name and image while sharing platform cursor handles and borrowed graphics.
class WnCursorBase : public WnCursor {
public:
  explicit WnCursorBase(const ScStringView &name);
  virtual ~WnCursorBase();
  bool LoadCursor(ScStringView *name) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetCursor() override;
  void GetHotSpot(i32 *x, i32 *y) override;

protected:
  WnCursorBase();
  bool hardware;
  GxGraphics *graphics;
  ScString filename;
  GxBuffer *image;
  i16 hotspot_x, hotspot_y;
  u32 references;
  void *cursor;
};

enum class WnStandardCursor : i32 {
  Arrow,
  Cross,
  IBeam,
  Yes,
  No,
  SizeAll,
  SizeNESW,
  SizeNS,
  SizeNWSE,
  SizeWE,
  UpArrow,
  DownArrow,
  RightArrow,
  LeftArrow,
  Wait,
  Help,
  Hand,
  Finger,
  Count
};

// The concrete system cursor overrides file loading and hotspot output with their original no-op bodies.
class WnSystemCursor : public WnCursorBase {
public:
  explicit WnSystemCursor(WnStandardCursor identifier);
  ~WnSystemCursor() override;
  bool LoadCursor(ScStringView *name) override;
  void GetHotSpot(i32 *x, i32 *y) override;
};

class ScMessageServer;

struct WnAcceleratorEntry {
  u32 key;
  ScMessage message;
};

// Actual accelerator ownership and message map; trivial entries replace the original general-purpose tree machinery.
class WnKeyAccelerator : public WnAccelerator {
public:
  WnKeyAccelerator();
  ~WnKeyAccelerator() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(WnAcceleratorResources *resources) override;
  bool Shutdown() override;
  bool RegisterAccelerator(u32 modifiers, u32 key, const ScMessage &message) override;
  bool RegisterAccelerator(u32 modifiers, u32 key, u32 command) override;
  bool UnregisterAccelerator(u32 key) override;
  bool ProcessAccelerator(const ScMessage &message) override;

protected:
  bool initialized;
  u32 references;
  ScArray<WnAcceleratorEntry> entries;
  ScMessageServer *messages;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Read a platform toggle bit without requiring a keyboard instance.
bool WnIsToggleKeySet(u32 key);

// The embedded GZWIND keyboard owns pressed-key order and a byte for each Windows virtual key.
class WnKeyboard {
public:
  WnKeyboard();
  ~WnKeyboard();
  virtual void ClearKeyboard();
  // MSVC reverses this overload group: the two-argument operation occupies slot4, the one-argument operation slot8.
  virtual bool IsKeyDown(u32 key);
  virtual bool IsKeyDown(u32 key, u32 modifiers);
  virtual bool IsKeyDownNow(u32 key);
  virtual bool CharIsModifier(u32 key);
  virtual u32 GetCurrentModifierState();
  virtual bool IsToggleKeySet(u32 key);
  virtual void ProcessKeyDown(u32 key);
  virtual void ProcessKeyUp(u32 key);

protected:
  struct WnKeyNode {
    WnKeyNode *next, *previous;
    u32 key;
  };

  WnKeyNode *head;
  u8 keys[256];
};

// The GZWIND cursor-manager implementation owns registered references, current selection and saved holds.
class WnCursors : public WnCursorManager {
public:
  WnCursors();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool SetCursor(u32 identifier) override;
  bool PushAutoCursor(u32 identifier) override;
  bool PopAutoCursor() override;
  bool AddCursor(WnCursor *cursor, u32 identifier) override;
  bool AddCursor(const ScStringView &name, u32 identifier) override;
  WnCursor *GetIGZCursor(u32 identifier) override;
  bool RemoveCursor(u32 identifier) override;
  virtual bool SetCursor(WnCursor *cursor);
  virtual ~WnCursors();

protected:
  struct WnCursorEntry {
    WnCursorEntry *next;
    u32 identifier;
    WnCursor *cursor;
  };

  WnCursorEntry *entries;
  ScArray<WnCursor *> saved;
  WnCursor *current;
  u32 references;
  void SetCurrentCursor(WnCursor *cursor);
  void ReleaseAllCursors();
  WnCursorEntry *Find(u32 identifier);
  void Erase(WnCursorEntry *entry);
};

#endif
