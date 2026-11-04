#include "application/newscast_ui.h"

#include <stdio.h>
#include <string.h>

#include <cstring>

#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/utf8.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/ui_resource_handles.h"
#include "ui/ui_resources.h"
#include "window/api.h"

// This cheat notification selects AddCheatCodeMessageByID(0), which lists names returned by GetSetCheatCodes.
// The input phrase is not established by the retained registration code.
enum ApNewscastCheatId : u32 {
  kApListSetCheatsId = 0xc3b2ed04,
};

// Construct the executable newscast and return its leading COM identity through the original secondary interface.
ScUnknown *ApNewscast::CreateNewObject() {
  return static_cast<ApNewscastControl *>(new ApNewscast);
}

ApNewscast::ApNewscast() : triggered_count(0), last_message(0) {
}

ApNewscast::~ApNewscast() {
  Shutdown();
}

// Publish the original newscast interface for its IID and delegate other queries to the ticker.
bool ApNewscast::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ApNewscastControl)
    return ApTicker::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ApNewscastControl *>(this);
  AddRef();
  return true;
}

u32 ApNewscast::AddRef() {
  return ApTicker::AddRef();
}

u32 ApNewscast::Release() {
  return ApTicker::Release();
}

// Return the ticker's borrowed window identity without adding a reference.
WnWindow *ApNewscast::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Look up message formatting and enqueue the message while holding the original recursive lock.
bool ApNewscast::AddNewscastMessage(ApNewsMessage *message) {
  lock.Lock();
  ApTickerInfo info;
  GetTickerInfo(message->group, message->instance, &info);
  AddNewscastMessage(message, &info);
  lock.Unlock();
  return true;
}

// Clear all queued messages and the active ticker while retaining the original lock across both operations.
bool ApNewscast::ClearAll() {
  lock.Lock();
  FreeAllQueues();
  last_message = 0;
  Clear();
  lock.Unlock();
  return true;
}

// Load ticker formatting, register the executable's message and cheat notifications, then initialize the ticker.
bool ApNewscast::Init() {
  if (GetInitFlag())
    return true;
  SetFont(ApGetUiResources()->GetFont(kWnFontUiSmall));
  if (!LoadTickerInfo())
    return false;
  static const u32 messages[] = {kApMsgCreateNotification, kApMsgCreateNotificationWithTickerInfo,
      kApMsgDismissNotification, kApMsgClearNotifications, kApMsgLockNotificationQueue, kApMsgUnlockNotificationQueue,
      kApMsgPreCityChange};

  struct ApCheatEntry {
    u32 id;
    const char *name;
  };

  static const ApCheatEntry cheats[] = {{kApFundCheatId, "fund"}, {kApAdvisorCheatId, "advisor"},
      {kApMoreMoneyCheatId, "moremoney"}, {kApMaxisCheatId, "maxis"}, {kAp1234CheatId, "1234"},
      {kApSc3kCheatId, "sc3k"}, {kApMayorCheatId, "mayor"}, {kApHelloCheatId, "hello"},
      {kApSimonSaysCheatId, "simon says"}, {kApSimCityCheatId, "simcity"}, {kApMoneyCheatId, "money"},
      {kApTickerCheatId, "ticker"}, {kApElectronicArtsCheatId, "electronic arts"}, {kApErtsCheatId, "erts"},
      {kApWillWrightCheatId, "will wright"}, {kApHelpCheatId, "help"}, {kApSimCheatId, "sim"},
      {kApEasterEggCheatId, "easter egg"}, {kApLlamaCheatId, "llama"}, {kApScurkCheatId, "scurk"},
      {kApBatCheatId, "bat"}, {kApPorntipsguzzardoCheatId, "porntipsguzzardo"}, {kApBroccoliCheatId, "broccoli"}};
  for (u32 message : messages)
    ApGetMessageServer()->AddNotification(this, message);
  ApGameInterface *game = ApGetGameApp();
  if (game) {
    for (const ApCheatEntry &entry : cheats) {
      ScString name(entry.name);
      game->GetCheatCodeManager()->RegisterCheatCode(entry.id, name);
    }
    for (const ApCheatEntry &entry : cheats)
      game->GetCheatCodeManager()->AddNotification(this, entry.id);
    game->GetCheatCodeManager()->AddNotification(this, kApListSetCheatsId);
  }
  triggered_count = 0;
  return ApTicker::Init();
}

// Enumerate the named ticker-data file through the same loose path and SYS.PAK package used by the executable.
void ApNewscast::TickerFileEnum(const ScString &, const ScString &value, ApIniContext *context) {
  ScString path;
  ApCompressedIni ini;
  ApGameInterface *game = ApGetGameApp();
  if (game) {
    game->GetAppDataDirectory(&path);
    path.Append("\\Sys\\");
    path.Append(value.Data(), value.Strlen());
    ini.SetPath(path);
    game->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SetCompressedPath(path);
    ScString section("Sc3TickerData");
    ini.EnumEntries(section, TickerInfoEnum, context);
  }
}

// Clear old ticker metadata and enumerate the executable's SC3Newscast.INI file list from loose or packaged resources.
bool ApNewscast::LoadTickerInfo() {
  ScString path;
  ApCompressedIni ini;
  ApGameInterface *game = ApGetGameApp();
  if (!game)
    return false;
  game->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Newscast.INI");
  ini.SetPath(path);
  game->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  FreeTickerInfo();
  ScString section("TickerData Files");
  return ini.EnumEntries(section, TickerFileEnum,
      reinterpret_cast<ApIniContext *>(static_cast<WnTickerControl *>(this)));
}

// Remove the ticker's message and cheat registrations before releasing its queues and shutting down its window.
bool ApNewscast::Shutdown() {
  static const u32 messages[] = {kApMsgCreateNotification, kApMsgCreateNotificationWithTickerInfo,
      kApMsgDismissNotification, kApMsgClearNotifications, kApMsgLockNotificationQueue, kApMsgUnlockNotificationQueue,
      kApMsgPreCityChange};
  static const u32 cheats[] = {kApFundCheatId, kApAdvisorCheatId, kApMoreMoneyCheatId, kApMaxisCheatId, kAp1234CheatId,
      kApSc3kCheatId, kApMayorCheatId, kApHelloCheatId, kApSimonSaysCheatId, kApSimCityCheatId, kApMoneyCheatId,
      kApTickerCheatId, kApElectronicArtsCheatId, kApErtsCheatId, kApWillWrightCheatId, kApHelpCheatId, kApSimCheatId,
      kApEasterEggCheatId, kApLlamaCheatId, kApScurkCheatId, kApBatCheatId, kApPorntipsguzzardoCheatId,
      kApBroccoliCheatId, kApListSetCheatsId, kUiCheat_UnlockBuildingCatalog};
  for (u32 message : messages)
    ApGetMessageServer()->RemoveNotification(this, message);
  ApGameInterface *game = ApGetGameApp();
  if (game) {
    for (u32 index = 0; index < 23; ++index)
      game->GetCheatCodeManager()->RemoveNotification(this, cheats[index]);
    for (u32 cheat : cheats)
      game->GetCheatCodeManager()->UnregisterCheatCode(cheat);
  }
  FreeTickerInfo();
  FreeAllQueues();
  HideWindow();
  return ApTicker::Shutdown();
}

// Erase all registered ticker formatting and generic-message keys.
bool ApNewscast::FreeTickerInfo() {
  ticker_info.clear();
  generic_messages.erase(generic_messages.begin(), generic_messages.end());
  return true;
}

// Destroy queued ticker messages in priority order, removing each queue node after deleting its item.
bool ApNewscast::FreeAllQueues() {
  for (auto &queue : queues) {
    while (!queue.empty()) {
      WnTickerItem *item = queue.front();
      if (item)
        delete item;
      queue.pop_front();
    }
  }
  return true;
}

// Convert the current city date to the original 30-day-month timestamp, returning zero without a city.
u32 ApNewscast::GetTimeStamp() {
  CtCity *city = ApGetCity();
  if (!city)
    return 0;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  return day + 30 * (month + 12 * year);
}

// Copy registered ticker formatting, or supply the original default priority and converted colors.
bool ApNewscast::GetTickerInfo(u32 group, u32 instance, ApTickerInfo *out) {
  auto position = ticker_info.find({group, instance});
  if (position != ticker_info.end()) {
    out->priority = position->second.priority;
    out->flags = position->second.flags;
    out->font_color = position->second.font_color;
    out->background_color = position->second.background_color;
    out->max_repeat = position->second.max_repeat;
    return true;
  }
  out->priority = 5;
  out->font_color = MakeFillColor(255, 255, 255);
  out->background_color = MakeFillColor(0, 0, 0);
  out->flags = 0;
  out->max_repeat = 1;
  return false;
}

// Register formatting once for a resource key and append priority-six keys to the generic-message vector.
bool ApNewscast::RegisterMessage(
    u32 group, u32 instance, u8 priority, u32 font_color, u32 background_color, bool display_flags, u32 max_repeat) {
  ApTickerKey key{group, instance};
  ApTickerInfo info{priority, static_cast<u8>(display_flags), font_color, background_color, max_repeat};
  if (!ticker_info.emplace(key, info).second)
    return false;
  if (priority == 6)
    generic_messages.push_back(key);
  return true;
}

// Remove a registered resource key and erase its first generic-message entry when its priority is six.
bool ApNewscast::UnregisterMessage(u32 group, u32 instance) {
  auto position = ticker_info.find({group, instance});
  if (position == ticker_info.end())
    return false;
  u8 priority = position->second.priority;
  ticker_info.erase(position);
  if (priority == 6) {
    for (auto entry = generic_messages.begin(); entry != generic_messages.end(); ++entry) {
      if (entry->group == group && entry->instance == instance) {
        generic_messages.erase(entry);
        return true;
      }
    }
  }
  return true;
}

// Copy the supplied text into an owned ticker item and append it to the selected bounded priority queue.
// The recursive lock also covers ID allocation, text retention and eviction of the oldest item.
bool ApNewscast::AddNewscastMessage(ApNewsMessage *message, ApTickerInfo *info) {
  lock.Lock();
  if (!message->text || !message->text->Strlen() || !info) {
    lock.Unlock();
    return false;
  }
  message->result = GetNewMessageID();
  if (message->message.type)
    info->flags = 1;
  ScString *copy = new ScString(*message->text);
  message->text = copy;
  copy->AddRef();
  u32 stamp = GetTimeStamp();
  WnTickerItem *item = new WnTickerItem(message->result, copy, font, info->font_color, info->background_color,
      info->flags != 0, stamp, info->max_repeat, &message->message, nullptr, nullptr);
  copy->Release();
  auto &queue = queues[info->priority];
  if (queue.size() >= 64) {
    WnTickerItem *oldest = queue.front();
    if (oldest)
      delete oldest;
    queue.pop_front();
  }
  queue.push_back(item);
  lock.Unlock();
  return true;
}

bool ApNewscast::RemoveItem(u32 requested) {
  return RemoveMessage(requested);
}

// Remove the first queued item with the requested identifier while holding the original recursive lock.
// Stop immediately after removal to exclude the executable's subsequent read from the erased list node.
bool ApNewscast::RemoveMessage(u32 id) {
  lock.Lock();
  for (auto &queue : queues) {
    for (auto entry = queue.begin(); entry != queue.end(); ++entry) {
      WnTickerItem *item = *entry;
      if (item->identifier == id) {
        queue.erase(entry);
        if (item)
          delete item;
        lock.Unlock();
        return true;
      }
    }
  }
  lock.Unlock();
  return false;
}

// Forward clicks on actionable ticker messages after notifying the game's event controller.
bool ApNewscast::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  WnTickerItem *item = FindClickedMessage(x);
  if (item && item->message.type) {
    ApGameInterface *game = ApGetGameApp();
    if (game) {
      ApBoxX *box = game->GetBoxX();
      if (box)
        box->Event(kAuEventPlayEffect, kAuEffectUiClickOnNewsticker, 0, 0, 0);
    }
    ApTicker::OnMouseDownL(x, y, modifiers);
  }
  return true;
}

static bool ApCheatMatches(const ScString *text, const char *keyword) {
  usize length = strlen(keyword);
  return text->Strlen() == length && memcmp(text->Data(), keyword, length) == 0;
}

// Dispatch newscast, queue-lock and cheat notifications, forwarding all other messages to the root window.
// Cheat text publication remains a direct dependency on the original AddCheatCodeMessageByID operation.
bool ApNewscast::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kApMsgCreateNotification:
    return AddNewscastMessage(reinterpret_cast<ApNewsMessage *>(message->data1));
  case kApMsgDismissNotification:
    return RemoveItem(static_cast<u32>(message->data1));
  case kApMsgClearNotifications:
  case kApMsgPreCityChange:
    return ClearAll();
  case kApMsgCreateNotificationWithTickerInfo:
    return AddNewscastMessage(
        reinterpret_cast<ApNewsMessage *>(message->data1), reinterpret_cast<ApTickerInfo *>(message->data2));
  case kApMsgLockNotificationQueue:
    lock.Lock();
    return true;
  case kApMsgUnlockNotificationQueue:
    lock.Unlock();
    return true;
  case kWnMsg_CheatCommand:
    break;
  default:
    return WnRootWidget::DoMessage(message);
  }
  u32 cheat = static_cast<u32>(message->data1), text_id;
  ScString *text = reinterpret_cast<ScString *>(message->data2);
  switch (cheat) {
  case kApFundCheatId:
    text_id = ApCheatMatches(text, "FUND") ? 805 : 806;
    break;
  case kApAdvisorCheatId:
    text_id = 807;
    break;
  case kApMoreMoneyCheatId:
    text_id = 810;
    break;
  case kApMaxisCheatId:
    text_id = ApCheatMatches(text, "MAXIS") ? 811 : 812;
    break;
  case kAp1234CheatId:
    text_id = 813;
    break;
  case kApSc3kCheatId:
    text_id = 814;
    break;
  case kApMayorCheatId:
    text_id = 815;
    break;
  case kApHelloCheatId:
    text_id = 816;
    break;
  case kApSimCityCheatId:
    text_id = 817;
    break;
  case kApMoneyCheatId:
    text_id = 818;
    break;
  case kApTickerCheatId:
    text_id = 819;
    break;
  case kApElectronicArtsCheatId:
    text_id = 820;
    break;
  case kApErtsCheatId:
    text_id = 821;
    break;
  case kApWillWrightCheatId:
    text_id = 822;
    break;
  case kApHelpCheatId:
    text_id = ApCheatMatches(text, "HELP") ? 823 : 824;
    break;
  case kApSimCheatId:
    text_id = ApCheatMatches(text, "SIM") ? 826 : 825;
    break;
  case kApEasterEggCheatId:
    text_id = 827;
    break;
  case kApLlamaCheatId:
    text_id = 828;
    break;
  case kApScurkCheatId:
    text_id = 829;
    break;
  case kApBatCheatId:
    text_id = 830;
    break;
  case kApPorntipsguzzardoCheatId:
    text_id = 831;
    break;
  case kApBroccoliCheatId:
    text_id = 832;
    break;
  case kApListSetCheatsId:
    text_id = 0;
    break;
  case kApSimonSaysCheatId: {
    ApTickerInfo info;
    GetTickerInfo(kRsNewsTextGroup, 833, &info);
    ScString *copy = new ScString(text->Mid(11, kScStringToEnd));
    copy->AddRef();
    ScMessage empty;
    u32 stamp = GetTimeStamp(), message_id = GetNewMessageID();
    WnTickerItem *item = new WnTickerItem(message_id, copy, font, info.font_color, info.background_color, 0, stamp,
        info.max_repeat, &empty, nullptr, nullptr);
    copy->Release();
    auto &queue = queues[info.priority];
    if (queue.size() >= 64) {
      delete queue.front();
      queue.pop_front();
    }
    queue.push_back(item);
    return true;
  }
  default:
    return WnRootWidget::DoMessage(message);
  }
  AddCheatCodeMessageByID(text_id);
  return true;
}

// Expire and select queued messages in the original priority order, or request a random message after a burst.
// Transfer the selected item to the ticker and requeue a retained copy while its repeat count permits it.
void ApNewscast::GetNextMessage() {
  lock.Lock();
  u32 now = GetTimeStamp();
  auto expire = [this, now](u32 priority, u32 age) {
    auto &queue = queues[priority];
    while (!queue.empty() && now - queue.front()->timestamp > age) {
      delete queue.front();
      queue.pop_front();
    }
  };
  auto take = [this](u32 priority) {
    auto &queue = queues[priority];
    last_message = queue.front()->identifier;
    AddMessageInternal(queue.front());
    WnTickerItem *item = queue.front();
    if (!item->times_left || --item->times_left)
      queue.push_back(new WnTickerItem(*queue.front()));
    queue.pop_front();
    ++triggered_count;
  };
  expire(0, 1080);
  if (!queues[0].empty()) {
    take(0);
  } else {
    expire(1, 1080);
    if (!queues[1].empty() && last_message != queues[1].front()->identifier) {
      take(1);
    } else {
      expire(3, 1080);
      if (!queues[3].empty() && now - queues[3].front()->timestamp > 360 &&
          last_message != queues[3].front()->identifier) {
        take(3);
      } else {
        expire(2, 90);
        if (!queues[2].empty() && last_message != queues[2].front()->identifier) {
          take(2);
        } else {
          bool selected = false;
          if (triggered_count < 5) {
            for (WnTickerItem *item : queues[3]) {
              if (now - item->timestamp < 360) {
                if (last_message != queues[3].front()->identifier) {
                  take(3);
                  selected = true;
                }
                break;
              }
            }
            if (!selected) {
              expire(4, 1080);
              if (!queues[4].empty() && last_message != queues[4].front()->identifier) {
                take(4);
                selected = true;
              } else {
                expire(5, 270);
                if (!queues[5].empty() && last_message != queues[5].front()->identifier) {
                  take(5);
                  selected = true;
                }
              }
            }
          }
          if (!selected) {
            WnTickerItem *item = CreateRandomMessage();
            if (item) {
              last_message = item->identifier;
              AddMessageInternal(item);
            }
            triggered_count = 0;
          }
        }
      }
    }
  }
  lock.Unlock();
}

// Choose a registered generic message and build a ticker item from its expanded resource text.
// Missing text uses the executable's single-space placeholder and its white-on-black colors.
WnTickerItem *ApNewscast::CreateRandomMessage() {
  if (generic_messages.empty())
    return nullptr;
  ScRandom random(kScRandomSeedFromClock);
  ApTickerKey key = generic_messages[random.Bounded(static_cast<u32>(generic_messages.size()))];
  ApTickerInfo info;
  GetTickerInfo(key.group, key.instance, &info);
  UiTextHandle resource(key.instance, key.group);
  ScString *value;
  u32 foreground, background, kind;
  if (resource.IsStringPresent()) {
    value = ScAddRefNotNull(new ScString());
    resource.Detokenize(value);
    foreground = info.font_color;
    background = info.background_color;
    kind = info.flags != 0;
  } else {
    background = MakeFillColor(0, 0, 0);
    value = new ScString(" ");
    foreground = MakeFillColor(255, 255, 255);
    value->AddRef();
    kind = 0;
  }
  u32 stamp = GetTimeStamp();
  u32 message_id = GetNewMessageID();
  WnTickerItem *item = new WnTickerItem(
      message_id, value, font, foreground, background, kind, stamp, info.max_repeat, nullptr, nullptr, nullptr);
  value->Release();
  return item;
}

void ApNewscast::DebugDumpMessages() {
}

// Extract one comma-terminated field, leaving the original remainder untouched when no comma exists.
static ScString ApTakeTickerField(ScString *value) {
  usize length = static_cast<usize>(value->end - value->begin);
  const char *comma = static_cast<const char *>(memchr(value->begin, ',', length));
  if (!comma)
    return ScString();
  u32 prefix_length = static_cast<u32>(comma - value->begin);
  ScString prefix(value->begin, prefix_length);
  ScString remainder(comma + 1, static_cast<u32>(length - prefix_length - 1));
  *value = remainder;
  return prefix;
}

// Decode one ticker-data entry into its resource key, priority, colors, flags and repeat count.
// The three reserved numeric fields are consumed in their original order but do not affect the stored record.
void ApNewscast::TickerInfoEnum(const ScString &key, const ScString &value, ApIniContext *owner) {
  ApNewscast *context = static_cast<ApNewscast *>(reinterpret_cast<WnTickerControl *>(owner));
  ScString remaining(key), field = ApTakeTickerField(&remaining);
  ApTickerKey ticker_key{ScConvertToUint32(field.ToChar()), ScConvertToUint32(remaining.ToChar())};
  remaining = value;
  field = ApTakeTickerField(&remaining);
  u8 priority = static_cast<u8>(ScConvertToUint32(field.ToChar()) - 1);
  field = ApTakeTickerField(&remaining);
  u8 red = static_cast<u8>(ScConvertToUint32(field.ToChar()));
  field = ApTakeTickerField(&remaining);
  u8 green = static_cast<u8>(ScConvertToUint32(field.ToChar()));
  field = ApTakeTickerField(&remaining);
  u8 blue = static_cast<u8>(ScConvertToUint32(field.ToChar()));
  u32 foreground = context->MakeFillColor(red, green, blue);
  for (u32 index = 0; index < 3; ++index) {
    field = ApTakeTickerField(&remaining);
    ScConvertToUint32(field.ToChar());
  }
  field = ApTakeTickerField(&remaining);
  u8 flags = static_cast<u8>(ScConvertToUint32(field.ToChar()));
  ApTickerInfo info{priority, flags, foreground, 0, ScConvertToUint32(remaining.ToChar())};
  context->ticker_info.emplace(ticker_key, info);
  if (priority == 6)
    context->generic_messages.push_back(ticker_key);
}

// Queue a localized cheat message, or assemble the currently set cheats when the requested identifier is zero.
// Preserve the original reverse traversal, exact-match filter, separator count and bounded priority queue.
u32 ApNewscast::AddCheatCodeMessageByID(u32 text_id) {
  UiTextHandle resource(text_id, kRsNewsTextGroup);
  if (!resource.IsStringPresent() && text_id)
    return 0;
  u32 message_id = GetNewMessageID();
  ApTickerInfo info;
  GetTickerInfo(kRsNewsTextGroup, text_id ? text_id : 832, &info);
  ScString value;
  if (text_id) {
    resource.Detokenize(&value);
  } else {
    ApGameInterface *game = ApGetGameApp();
    ScCheatCodeManager *cheats = game ? game->GetCheatCodeManager() : nullptr;
    if (!cheats)
      return 0;
    u32 count = 0;
    count = cheats->GetSetCheatCodes(nullptr, &count);
    if (count) {
      u32 name_count = 200;
      ScStringView **names = new ScStringView *[name_count];
      cheats->GetListOfAllCheatStrings(names, &name_count);
      u32 *set = new u32[count];
      count = cheats->GetSetCheatCodes(set, &count);
      if (!name_count)
        value.Append(" ");
      u32 remaining = count;
      while (name_count) {
        --name_count;
        u32 matched = 0;
        if (cheats->DoesCheatCodeMatch(*names[name_count], &matched) == 2) {
          for (u32 index = count; index; --index) {
            if (set[index - 1] == matched) {
              value.Append(names[name_count]->ToChar());
              if (--remaining)
                value.Append("; ");
              break;
            }
          }
        }
      }
      delete[] set;
      delete[] names;
    } else {
      value.Append(" ");
    }
  }
  ScString *copy = new ScString(value);
  copy->AddRef();
  ScMessage empty;
  u32 stamp = GetTimeStamp();
  WnTickerItem *item = new WnTickerItem(message_id, copy, font, info.font_color, info.background_color, 0, stamp,
      info.max_repeat, &empty, nullptr, nullptr);
  copy->Release();
  auto &queue = queues[info.priority];
  if (queue.size() >= 64) {
    delete queue.front();
    queue.pop_front();
  }
  queue.push_back(item);
  return message_id;
}

// Construct the executable's ticker state and queues, then request its buffer through the application graphics service.
ApTicker::ApTicker()
    : timer(ScStopwatch::Milliseconds, 0), phase(0), buffer(nullptr), current_kind(0), language(0),
      next_character(nullptr), next_id(0), font(nullptr), active_font(nullptr), current_color(0), current_end_color(0),
      step(2), interval(50) {
  ApGetGraphics()->CreateBuffer(&buffer);
}

ApTicker::~ApTicker() {
  ApTicker::Shutdown();
}

bool ApTicker::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnTickerControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnTickerControl *>(this);
  AddRef();
  return true;
}

u32 ApTicker::AddRef() {
  return WnRootWidget::AddRef();
}

u32 ApTicker::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root-window identity associated with the leading ticker interface.
WnWindow *ApTicker::GetWindow() {
  return static_cast<WnWindow *>(this);
}

u32 ApTicker::GetNewMessageID() {
  return ++next_id;
}

void ApTicker::SetSpeed(u32 speed) {
  step = speed;
}

void ApTicker::SetFrequency(u32 milliseconds) {
  interval = milliseconds;
}

void ApTicker::SetCaption(const ScStringView &) {
}

bool ApTicker::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  return true;
}

bool ApTicker::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool ApTicker::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Initialize the backing buffer, cursor and language through the executable's own application services.
bool ApTicker::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  phase = 0;
  timer.Restart();
  SetH(font->Height(0));
  u32 height = draw_area.bottom - draw_area.top, width = draw_area.right - draw_area.left;
  if (!buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
    return false;
  buffer->SetTransparency(ApGetUiResources()->GetColor(kWnColorUiTransparency));
  if (buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  item_cursor = active.head;
  WnCursorManager *manager = owner->GetCursorManager();
  WnCursor *selected = manager->GetIGZCursor(17);
  SetCursor(selected, false);
  ScLanguageManager *languages = ApGetLanguageManager();
  language = languages ? languages->GetCurrentLanguage() : 1;
  return WnRootWidget::Init();
}

u32 ApTicker::AddMessage(
    ScStringView *text, u32 color, u32 end_color, u32 kind, WnTickerCallback callback, void *context) {
  if (!font)
    return 0;
  FtFont *selected = font;
  u32 message_id = GetNewMessageID();
  auto *item = new WnTickerItem(message_id, text, selected, color, end_color, kind, 0, 0, nullptr, callback, context);
  pending.Append(item);
  return item->identifier;
}

u32 ApTicker::AddMessage(ScStringView *text, u32 color, u32 end_color, u32 kind, const ScMessage *message) {
  if (!font)
    return 0;
  FtFont *selected = font;
  u32 message_id = GetNewMessageID();
  auto *item = new WnTickerItem(message_id, text, selected, color, end_color, kind, 0, 0, message, nullptr, nullptr);
  pending.Append(item);
  return item->identifier;
}

// Unlink and destroy the first matching pending message, leaving active messages untouched.
bool ApTicker::RemoveItem(u32 requested) {
  for (WnTickerNode *node = pending.head->next; node != pending.head; node = node->next) {
    WnTickerItem *item = node->item;
    if (item->identifier == requested) {
      pending.Erase(node);
      delete item;
      return true;
    }
  }
  return false;
}

// Destroy pending and active items, then refill the backing buffer if its fill lock succeeds.
bool ApTicker::Clear() {
  while (pending.head->next != pending.head) {
    delete pending.head->next->item;
    pending.Erase(pending.head->next);
  }
  while (active.head->next != active.head) {
    delete active.head->next->item;
    active.Erase(active.head->next);
  }
  if (buffer && buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  return true;
}

bool ApTicker::Shutdown() {
  Clear();
  while (active.head->next != active.head) {
    delete active.head->next->item;
    active.Erase(active.head->next);
  }
  ScRelease(&font);
  ScRelease(&buffer);
  return WnRootWidget::Shutdown();
}

// Find the first active message whose signed horizontal span contains the requested coordinate.
WnTickerItem *ApTicker::FindClickedMessage(i32 x) {
  for (WnTickerNode *node = active.head->next; node != active.head; node = node->next) {
    WnTickerItem *item = node->item;
    if (x >= item->span.left && x < item->span.right)
      return item;
  }
  return nullptr;
}

bool ApTicker::OnMouseDownL(i32 x, i32, u32) {
  WnTickerItem *item = FindClickedMessage(x);
  if (item) {
    if (item->callback)
      item->callback(item->identifier, item->context);
    if (item->message.type)
      ApGetMessageServer()->MessagePost(item->message, false);
  }
  return true;
}

// Transfer the first pending item into the active queue through the original virtual insertion method.
void ApTicker::GetNextMessage() {
  if (pending.head->next != pending.head) {
    WnTickerItem *item = pending.head->next->item;
    pending.Erase(pending.head->next);
    AddMessageInternal(item);
  }
}

static i32 ApTickerAdd(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) + static_cast<u32>(second));
}

// Position and transfer an item into the active queue, initializing the current drawing fields on the first transfer.
void ApTicker::AddMessageInternal(WnTickerItem *item) {
  if (!item)
    return;
  item->span.left = ApTickerAdd(GetW(), 20);
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  i32 width = font->Measure(bytes, length);
  item->span.right = ApTickerAdd(ApTickerAdd(width, GetW()), 20);
  active.Append(item);
  if (!active_font) {
    active_font = item->font;
    current_kind = item->kind;
    current_color = item->color;
    current_end_color = item->end_color;
  }
}

// Request the next message, then select the last active item and reset its entrance position and character cursor.
void ApTicker::AdvanceMessage() {
  GetNextMessage();
  if (active.head->next == active.head) {
    next_character = nullptr;
    item_cursor = active.head->next;
    return;
  }
  item_cursor = active.head->previous;
  WnTickerItem *item = item_cursor->item;
  active_font = item->font;
  current_kind = item->kind;
  current_color = item->color;
  current_end_color = item->end_color;
  next_character = item->text->Data();
  item->span.left = ApTickerAdd(GetW(), 20);
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  i32 width = active_font->Measure(bytes, length);
  item->span.right = ApTickerAdd(ApTickerAdd(GetW(), width), 20);
  phase = -20;
}

// Shift active spans and destroy items that have exited, advancing away from the current cursor before removal.
void ApTicker::ShiftSpans() {
  WnTickerNode *head = active.head, *node = head->next;
  while (node != head) {
    WnTickerItem *item = node->item;
    item->span.left = static_cast<i32>(static_cast<u32>(item->span.left) - step);
    item->span.right = static_cast<i32>(static_cast<u32>(item->span.right) - step);
    if (item->span.right >= 0) {
      node = node->next;
    } else {
      WnTickerNode *removed = node;
      node = node->next;
      if (item_cursor == removed)
        AdvanceMessage();
      delete removed->item;
      active.Erase(removed);
    }
  }
}

static i32 ApTickerSubtract(i32 first, u32 second) {
  return (i32)((u32)first - second);
}

// Scroll the backing buffer, advance message spans and draw the next character using the current language and font.
void ApTicker::DoDraw() {
  i32 width = buffer->Width(), height = buffer->Height();
  ScRect source{(i32)step, 0, width, height}, destination{0, 0, ApTickerSubtract(width, step), height};
  if (buffer->Lock(GxLockFlags::Blt)) {
    buffer->Blt(buffer, source, destination, nullptr);
    buffer->Unlock(GxLockFlags::Blt);
  }
  if (buffer->Lock(GxLockFlags::Fill)) {
    ScRect strip{ApTickerSubtract(width, step), 0, width, height};
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, strip, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  phase = (i32)((u32)phase + step);
  ShiftSpans();
  if (active.head->next == active.head)
    AdvanceMessage();
  if (!next_character) {
    AdvanceMessage();
    return;
  }
  WnTickerItem *item = item_cursor->item;
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  if ((uptr)next_character >= (uptr)bytes + length) {
    AdvanceMessage();
    return;
  }
  const GxBltFlags *color = buffer->GetBltFlags();
  if (item->end_color != color->pixel && buffer->Lock(GxLockFlags::Fill)) {
    ScRect strip{ApTickerSubtract(width, phase), 0, width, height};
    buffer->Fill(item->end_color, strip, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  if ((current_kind & 1) && buffer->Lock(GxLockFlags::Line)) {
    i32 bottom = ApTickerSubtract(height, 1);
    buffer->DrawLine(ApTickerSubtract(width, phase), bottom, width, bottom, static_cast<i32>(current_color), 1);
    buffer->Unlock(GxLockFlags::Line);
  }
  bool multi = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageThai || language == kScLanguageKorean;
  if (multi)
    active_font->Color();
  u32 scalar, remaining = length - static_cast<u32>(next_character - bytes);
  u32 count = ScUtf8Decode(next_character, remaining, &scalar);
  if (!count || phase <= 0)
    return;
  bool drawing = active_font->BeginDraw(buffer);
  if (drawing) {
    active_font->DrawTextA(
        buffer, (u32)ApTickerSubtract(width, phase), 0, next_character, count, current_color, current_end_color);
    active_font->EndDraw(buffer);
  }
  if (!drawing && multi)
    return;
  i32 advance = font->Advance(scalar);
  if (phase >= advance) {
    next_character += count;
    phase = ApTickerSubtract(phase, advance);
  }
}

// Advance after the original strict timer threshold and copy the backing buffer to the window drawing surface.
bool ApTicker::GZPaint() {
  if (timer.GetElapsedTime() > interval) {
    timer.Restart();
    DoDraw();
    InvalidateSelf();
  }
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    const ScRect *source = buffer->GetBufferArea();
    draw_buffer->Blt(buffer, *(source), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}
