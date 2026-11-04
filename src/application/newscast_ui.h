#ifndef SC3_APPLICATION_NEWSCAST_UI_H
#define SC3_APPLICATION_NEWSCAST_UI_H

#include <list>
#include <map>
#include <vector>

#include "application/api.h"
#include "common/thread.h"
#include "window/lists.h"

// The executable owns its own cGZWinTextTicker copy and resolves services through application globals.
// Unimplemented original methods remain direct declarations until their executable bodies are reconstructed.
class ApTicker : public WnTickerControl, public WnRootWidget {
public:
  ApTicker();
  ~ApTicker() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  u32 AddMessage(ScStringView *text, u32 color, u32 end_color, u32 kind, const ScMessage *message) override;
  u32 AddMessage(
      ScStringView *text, u32 color, u32 end_color, u32 kind, WnTickerCallback callback, void *context) override;
  bool Clear() override;
  bool RemoveItem(u32 identifier) override;
  bool SetFont(FtFont *font) override;
  void SetSpeed(u32 speed) override;
  void SetFrequency(u32 milliseconds) override;
  bool Init() override;
  bool Shutdown() override;
  void SetCaption(const ScStringView &text) override;
  bool GZPaint() override;
  virtual void GetNextMessage();

protected:
  virtual void AddMessageInternal(WnTickerItem *item);
  u32 GetNewMessageID();
  WnTickerItem *FindClickedMessage(i32 x);
  ScStopwatch timer;
  void AdvanceMessage();
  void ShiftSpans();
  void DoDraw();
  i32 phase;
  GxBuffer *buffer;
  u32 current_kind;
  i32 language;
  WnTickerNode *item_cursor;
  const char *next_character;
  u32 next_id;
  FtFont *font, *active_font;
  u32 current_color, current_end_color;
  WnTickerQueue active, pending;
  u32 step, interval;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
};

struct ApIniContext;

// Retail tTickerKey, tTickerInfo and tNewsMsgInfo preserve the executable's actual resource and message fields.
struct ApTickerKey {
  u32 group, instance;
};

// The original map orders unsigned resource groups first, then unsigned instance identifiers.
struct ApTickerKeyLess {
  // Compare both original unsigned resource-key words in their observed map order.
  bool operator()(const ApTickerKey &first, const ApTickerKey &second) const {
    return first.group < second.group || (first.group == second.group && first.instance < second.instance);
  }
};

struct ApTickerInfo {
  u8 priority, flags;
  u32 font_color, background_color, max_repeat;
};

struct ApNewsMessage {
  u32 result, group, instance;
  ScStringView *text;
  ScMessage message;
};

// Executable cSC3Newscast owns the proven map, vector, six message queues and recursive lock.
class ApNewscast : public ApTicker, public ApNewscastControl {
public:
  ApNewscast();
  ~ApNewscast() override;
  static ScUnknown *CreateNewObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool RemoveItem(u32 requested) override;
  void GetNextMessage() override;
  WnWindow *AsIGZWin() override;
  bool AddNewscastMessage(ApNewsMessage *message) override;
  bool RemoveMessage(u32 identifier) override;
  bool ClearAll() override;
  bool RegisterMessage(
      u32 group, u32 instance, u8 priority, u32 font_color, u32 background_color, bool flags, u32 max_repeat) override;
  bool UnregisterMessage(u32 group, u32 instance) override;

protected:
  bool AddNewscastMessage(ApNewsMessage *message, ApTickerInfo *info);
  bool FreeTickerInfo();
  bool FreeAllQueues();
  bool GetTickerInfo(u32 group, u32 instance, ApTickerInfo *out);
  bool LoadTickerInfo();
  u32 AddCheatCodeMessageByID(u32 identifier);
  WnTickerItem *CreateRandomMessage();
  void DebugDumpMessages();
  static void TickerInfoEnum(const ScString &key, const ScString &value, ApIniContext *context);
  static void TickerFileEnum(const ScString &key, const ScString &value, ApIniContext *context);
  static u32 GetTimeStamp();
  std::map<ApTickerKey, ApTickerInfo, ApTickerKeyLess> ticker_info;
  std::vector<ApTickerKey> generic_messages;
  std::list<WnTickerItem *> queues[6];
  u32 triggered_count, last_message;
  ScLock lock;
};

#endif
