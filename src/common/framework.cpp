#include "common/framework.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "application/application_core.h"
#include "common/api.h"
#include "common/cheat_manager.h"
#include "common/ids.h"
#include "common/locale.h"
#include "common/sc3_debug_stream.h"
#include "common/sc3_error.h"
#include "common/sc3_qfs.h"
#include "common/sc3_stream.h"
#include "common/sc3_unknown.h"

// The native debug window and its edit control retain their original complete Win32 style words.
enum ScDebugWindowStyle : u32 {
  kScDebugFrameWindowStyle = 0xef0000,
  kScDebugEditWindowStyle = 0x50801044,
};

// Application initialization owns this inclusive priority band; the surrounding phases retain the outside ranges.
enum ScAppInitPriorityBand : i32 {
  kScAppInitHighestPriority = -2000000,
  kScAppInitLowestPriority = -2999999,
};

// The service constructor supplies this original startup ordering priority.
enum ScConsoleServiceOrder : i32 {
  kScConsoleServicePriority = 0x3d0900,
};

static void ScLowerCheatText(ScString *text) {
  text->MakeLower();
}

static bool ScCheatPrefix(const ScString &text, const ScString &prefix) {
  u32 length = prefix.Strlen();
  return text.Strlen() >= length && (!length || memcmp(text.Data(), prefix.Data(), length) == 0);
}

// Read the stored little-endian allocation length without imposing a new codec result contract.
static u32 ScCheatPasswordLength(const u8 *password) {
  u32 length;
  memcpy(&length, password, sizeof(length));
  return length;
}

// Assert the header preconditions for the shared size reader. Malformed blobs lie outside the original defined domain.
static u32 ScCheatPasswordSize(const u8 *password) {
  assert(ScCheatPasswordLength(password) >= 6);
  assert(ScCheatPasswordLength(password) >= (password[4] == 0x10 && password[5] == 0xfb ? 6u : 9u) + 3);
  return ScQfsGetSize(password + 4);
}

ScCheatManager::ScCheatManager() : occurrences(0), password(nullptr), enabled(true) {
}

ScCheatManager::~ScCheatManager() {
  delete[] password;
  password = nullptr;
}

// Publish the primary Unknown or cheat-manager identity before invoking its no-op retain. Unsupported identities clear
// the output.
bool ScCheatManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCheatManager);
  HANDLE_QUERY_INTERFACE(IID_ScCheatManager, ScCheatManager);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScCheatManager::AddRef() {
  return 0;
}

u32 ScCheatManager::Release() {
  return 0;
}

// Reject an already registered spelling through the supplied String's equality operation, then append a copied association.
// Identifier reuse with a different spelling is permitted.
bool ScCheatManager::RegisterCheatCode(u32 identifier, const ScStringView &name) {
  for (const auto &entry : names)
    if (name.IsEqual(&entry.text))
      return false;
  ScCheatName entry;
  {
    ScString copy(name);
    entry.text = copy;
  }
  entry.identifier = identifier;
  names.push_back(entry);
  return true;
}

// Remove only the first name associated with the identifier, leaving its set state and subscriptions untouched.
bool ScCheatManager::UnregisterCheatCode(u32 identifier) {
  for (auto it = names.begin(); it != names.end(); ++it)
    if (it->identifier == identifier) {
      names.erase(it);
      return true;
    }
  return false;
}

// Append a unique target/identifier pair before retaining its target. Zero identifiers subscribe to every cheat.
bool ScCheatManager::AddNotification(ScMessageTarget *target, u32 identifier) {
  for (const auto &entry : notifications)
    if (entry.target == target && entry.identifier == identifier)
      return false;
  notifications.push_back({target, identifier});
  target->AddRef();
  return true;
}

// Erase the matching subscription before releasing its target, so callbacks observe the retired association.
bool ScCheatManager::RemoveNotification(ScMessageTarget *target, u32 identifier) {
  for (auto it = notifications.begin(); it != notifications.end(); ++it)
    if (it->target == target && it->identifier == identifier) {
      notifications.erase(it);
      target->Release();
      return true;
    }
  return false;
}

bool ScCheatManager::GetCheatHasBeenSet(u32 identifier) {
  return set_codes.find(identifier) != set_codes.end();
}

bool ScCheatManager::SetCheatHasBeenSet(u32 identifier, bool value) {
  if (value)
    set_codes.insert(identifier);
  else
    set_codes.erase(identifier);
  return true;
}

// Query the full set size with a null buffer, or copy its sorted prefix up to the supplied capacity.
// The reported count is also the direct result.
u32 ScCheatManager::GetSetCheatCodes(u32 *out, u32 *count) {
  u32 size = static_cast<u32>(set_codes.size());
  if (out) {
    if (*count > size)
      *count = size;
    auto it = set_codes.begin();
    for (u32 index = 0; index < *count; ++index)
      out[index] = *it++;
  } else
    *count = size;
  return *count;
}

void ScCheatManager::ClearAllSetCheatCodes() {
  set_codes.clear();
}

// Notify matching retained targets in list order while enabled, preserving the live text pointer and reused message.
// The original returns false even when notifications were delivered.
bool ScCheatManager::SendCheatNotifications(const ScStringView &text, u32 identifier) {
  if (enabled) {
    ScMessage message;
    for (auto it = notifications.begin(); it != notifications.end(); ++it)
      if (it->identifier == identifier || !it->identifier) {
        message.type = kWnMsg_CheatCommand;
        message.data1 = identifier;
        message.data2 = reinterpret_cast<uptr>(&text);
        it->target->DoMessage(&message);
      }
  }
  return false;
}

// Find the first case-insensitive registered prefix, returning one for a prefix and two for an exact match.
// Disabled or empty inputs leave the identifier output unchanged.
i32 ScCheatManager::DoesCheatCodeMatch(const ScStringView &text, u32 *identifier) {
  if (!enabled || !text.Strlen())
    return 0;
  ScString input(text), candidate;
  ScLowerCheatText(&input);
  for (const auto &entry : names) {
    candidate = entry.text;
    ScLowerCheatText(&candidate);
    if (ScCheatPrefix(input, candidate)) {
      *identifier = entry.identifier;
      return input.Strlen() == candidate.Strlen() ? 2 : 1;
    }
  }
  return 0;
}

// Recognize configured enable/disable prefixes after space trimming and lowercasing, passing their remainder as password.
// Ordinary cheats dispatch the original input and increment occurrences only after the notification pass.
bool ScCheatManager::DoDefaultCheatCodeProcessing(const ScStringView &text, u32 *identifier) {
  if (enable_text.Strlen() || disable_text.Strlen()) {
    ScString input(text), password_text;
    input.LTrim();
    input.RTrim();
    ScLowerCheatText(&input);
    if (ScCheatPrefix(input, enable_text)) {
      password_text.FromChar(input.Data() + enable_text.Strlen(), input.Strlen() - enable_text.Strlen());
      password_text.LTrim();
      EnableCheatCodes(true, password_text);
      return true;
    }
    if (ScCheatPrefix(input, disable_text)) {
      password_text.FromChar(input.Data() + disable_text.Strlen(), input.Strlen() - disable_text.Strlen());
      password_text.LTrim();
      EnableCheatCodes(false, password_text);
      return true;
    }
  }
  if (enabled && DoesCheatCodeMatch(text, identifier)) {
    SendCheatNotifications(text, *identifier);
    ++occurrences;
    return true;
  }
  return false;
}

// Publish borrowed name interfaces up to the supplied capacity, returning true even for an empty result.
bool ScCheatManager::GetListOfAllCheatStrings(ScStringView **out, u32 *count) {
  u32 capacity = *count;
  *count = 0;
  for (auto &entry : names) {
    if (*count >= capacity)
      break;
    out[(*count)++] = &entry.text;
  }
  return true;
}

u32 ScCheatManager::GetCheatCodeOccurrenceCount() {
  return occurrences;
}

// Disable with an optional QFS password, notifying while the old enabled state is still visible.
// Re-enabling checks the original length/decoder rules and publishes enabled before notifying its observers.
bool ScCheatManager::EnableCheatCodes(bool value, const ScStringView &key) {
  if (key.Strlen() > 256)
    return false;
  if (value) {
    if (enabled)
      return false;
    if (password) {
      u32 size = ScCheatPasswordSize(password);
      if (key.Strlen() != size)
        return false;
      u8 decoded[1024];
      if (size <= 256) {
#ifndef NDEBUG
        ScQfsAssertBounds(password + 4, ScCheatPasswordLength(password) - 4, sizeof(decoded));
#endif
        size = ScQfsDecompress(password + 4, nullptr, decoded);
        if (size && memcmp(key.Data(), decoded, size) != 0)
          return false;
      }
      delete[] password;
      password = nullptr;
    }
    enabled = true;
    ScString empty;
    SendCheatNotifications(empty, kScCheat_CheatsEnabled);
    return true;
  }
  if (!enabled)
    return false;
  if (key.Strlen()) {
    u8 bytes[1028];
    i32 fast = 1;
    u32 size = key.Strlen();
    u32 stored = RsQfsEncode(reinterpret_cast<const u8 *>(key.Data()), static_cast<i32>(size), bytes + 4, &fast) + 4;
    if (!stored)
      return false;
    memcpy(bytes, &stored, 4);
    password = new u8[stored];
    memcpy(password, bytes, stored);
  }
  ScString empty;
  SendCheatNotifications(empty, kScCheat_CheatsDisabled);
  enabled = false;
  return true;
}

bool ScCheatManager::GetCheatCodesEnabled() {
  return enabled;
}

// Query or copy the complete stored blob, publishing the required size before a short-buffer refusal.
bool ScCheatManager::GetCheatCodePassword(u8 *out, u32 *length) {
  if (password) {
    u32 size = ScCheatPasswordLength(password);
    if (out) {
      if (size > *length) {
        *length = size;
        return false;
      }
      memcpy(out, password, size);
    }
    *length = size;
  } else
    *length = 0;
  return true;
}

// Replace the password blob and validate its stored allocation length. Valid input also enables cheats.
// Reject shorter native input at this external blob boundary to exclude the original undefined size-word read.
bool ScCheatManager::SetCheatCodePassword(u8 *data, u32 length) {
  delete[] password;
  password = nullptr;
  if (!data)
    return true;
  if (length < 4)
    return false;
  password = new u8[length];
  memcpy(password, data, length);
  if (ScCheatPasswordLength(password) == length) {
    enabled = true;
    return true;
  }
  delete[] password;
  password = nullptr;
  return false;
}

// Copy and lowercase both command prefixes without trimming their registered spaces.
bool ScCheatManager::SetEnableDisableStrings(const ScStringView &enable, const ScStringView &disable) {
  {
    ScString copy(enable);
    enable_text = copy;
  }
  ScLowerCheatText(&enable_text);
  {
    ScString copy(disable);
    disable_text = copy;
  }
  ScLowerCheatText(&disable_text);
  return true;
}

void ScCheatManager::AddCheatCodeHistoryRecord(u32 identifier, u32 date, u32 reserved1, u32 reserved2) {
  history.push_back({identifier, date, reserved1, reserved2});
}

// Report the clamped count but copy the original complete history when a destination is supplied.
// The original caller must provide storage for every record even when its reported capacity is smaller.
u32 ScCheatManager::GetCheatCodeHistoryRecords(ScCheatHistory *out, u32 *count) {
  u32 size = static_cast<u32>(history.size());
  if (out) {
    if (*count > size)
      *count = size;
    if (size)
      memcpy(out, history.data(), static_cast<usize>(size) * sizeof(*out));
  } else
    *count = size;
  return *count;
}


ScDebugCommandBase::ScDebugCommandBase(const ScString &name, const ScString &help) : command(name), usage(help) {
}

ScDebugCommandBase::~ScDebugCommandBase() {
}

void ScDebugCommandBase::GetCommandString(ScStringView *out) {
  out->FromChar(command.ToChar());
}

void ScDebugCommandBase::GetUsageString(ScStringView *out) {
  out->FromChar(usage.ToChar());
}

void ScDebugCommandBase::Execute(const char *) {
}

ScDebugStreamBase::ScDebugStreamBase(u32 identifier, i32 priority) : ScServiceBase(identifier, priority) {
}

ScDebugStreamBase::~ScDebugStreamBase() {
}

// Publish each supported debug, command or output identity and retain the shared service object.
bool ScDebugStreamBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScDebugStream, ScDebugStream);
  HANDLE_QUERY_INTERFACE(IID_ScDebugCommandService, ScDebugCommandService);
  HANDLE_QUERY_INTERFACE(IID_ScOutputStream, ScOutputStream);
  return ScServiceBase::QueryInterface(iid, out);
}

u32 ScDebugStreamBase::AddRef() {
  return ScServiceBase::AddRef();
}

u32 ScDebugStreamBase::Release() {
  return RemoveRef();
}

bool ScDebugStreamBase::Write(const ScStringView &text) {
  return Write(text.ToChar());
}

// Write the string and return the distinct output-stream identity.
ScOutputStream *ScDebugStreamBase::operator<<(const ScStringView &text) {
  Write(text);
  return static_cast<ScOutputStream *>(this);
}

// Write the character string and return the distinct output-stream identity.
ScOutputStream *ScDebugStreamBase::operator<<(const char *text) {
  Write(text);
  return static_cast<ScOutputStream *>(this);
}

void ScDebugStreamBase::Flush() {
}

void ScDebugStreamBase::SetRZCharStr(const char *text) {
  Write(text);
}

void ScDebugStreamBase::RegisterCommand(ScDebugCommand *command) {
  commands.push_back(command);
}

void ScDebugStreamBase::UnregisterCommand(ScDebugCommand *command) {
  commands.erase(std::remove(commands.begin(), commands.end(), command), commands.end());
}

// Split the original command text on literal spaces and append each nonempty field to the argument vector.
static i32 ScParseDebugArguments(const ScString &text, std::vector<ScString> *arguments) {
  u32 position = 0, length = text.ScString::Strlen();
  i32 count = 0;
  while (position < length) {
    while (position < length && text.begin[position] == ' ')
      ++position;
    if (position == length)
      break;
    u32 start = position;
    while (position < length && text.begin[position] != ' ')
      ++position;
    arguments->emplace_back(text.begin + start, position - start);
    ++count;
  }
  return count;
}

// Parse literal-space-separated command arguments while holding the original recursive section.
i32 ScDebugStreamBase::ParseCommandLine(const ScString &text, std::vector<ScString> *arguments) {
  lock.Lock();
  i32 count = ScParseDebugArguments(text, arguments);
  lock.Unlock();
  return count;
}

// Print the stored name and usage of every command matching the supplied text.
void ScDebugStreamBase::DisplayCommand(const char *text) {
  assert(text);
  ScString name, usage, message;
  lock.Lock();
  for (ScDebugCommand *command : commands) {
    command->GetCommandString(&name);
    command->GetUsageString(&usage);
    if (!std::strcmp(name.ToChar(), text)) {
      message.Sprintf("%s - usage: %s", name.ToChar(), usage.ToChar());
      Write(message);
    }
  }
  lock.Unlock();
}

// Parse the command, dispatch matching registrations, or print every command's name and usage for help.
void ScDebugStreamBase::ExecuteCommand(ScStringView *input) {
  ScString text(*input), command_name;
  std::vector<ScString> arguments;
  if (!ParseCommandLine(text, &arguments))
    return;
  ScString help_lower("help"), help_upper("Help");
  if (!arguments[0].IsEqual(&help_lower) && !arguments[0].IsEqual(&help_upper)) {
    for (ScDebugCommand *command : commands) {
      command->GetCommandString(&command_name);
      if (command_name.IsEqual(&arguments[0]))
        command->Execute(text.ToChar());
    }
    return;
  }
  ScString name, usage;
  for (ScDebugCommand *command : commands) {
    command->GetCommandString(&name);
    command->GetUsageString(&usage);
    name.Append(" ");
    name.Append(usage.ToChar());
    name.Append("\n");
    Write(name.ToChar());
  }
}

// Save each stored line and a newline to the requested file, stopping on a short write.
bool ScDebugStreamBase::SaveToFile(const ScStringView &path) {
  RsFile file(path);
  if (!file.Open(kRsFileAccessWrite, kRsFileOpenAlways, kRsFileShareRead))
    return false;
  for (const ScString &line : lines) {
    u32 expected = line.ScString::Strlen(), written = expected;
    file.Write(line.begin, &written);
    if (written != expected)
      break;
    written = 1;
    file.Write("\n", &written);
    if (written != 1)
      break;
  }
  file.Close();
  return true;
}

// Retain the original line splitting and bounded history for both platform transports. The caller holds the stream lock.
void ScDebugStreamBase::AppendLines(const char *text) {
  ScString remaining(text), line, token;
  do {
    if (remaining.begin != remaining.end && *remaining.begin == '\n') {
      line.FromChar("", 0);
      lines.push_back(line);
      remaining.Erase(0, 1);
      token.FromChar("\n");
    } else {
      if (!lines.empty()) {
        line = lines.back();
        lines.pop_back();
      }
      token = remaining.SplitToken("\n");
      if (token.begin != token.end)
        line.Append(token.ToChar());
      lines.push_back(line);
      if (token.begin != token.end) {
        line.FromChar("", 0);
        lines.push_back(line);
      }
      while (lines.size() > 2500)
        lines.pop_front();
    }
  } while (token.begin != token.end);
  if (remaining.begin != remaining.end) {
    if (!lines.empty()) {
      line = lines.back();
      lines.pop_back();
    }
    line.Append(remaining.begin, remaining.ScString::Strlen());
    lines.push_back(line);
    while (lines.size() > 2500)
      lines.pop_front();
  }
}

ScConsoleDebugStream::ScConsoleDebugStream() : ScDebugStreamBase(kScOutputStreamClassId, kScConsoleServicePriority) {
}

// Record the original bounded history and write its bytes to stderr under the same recursive lock.
bool ScConsoleDebugStream::Write(const char *text) {
  lock.Lock();
  AppendLines(text);
  usize length = std::strlen(text);
  bool written = std::fwrite(text, 1, length, stderr) == length;
  lock.Unlock();
  return written;
}

void ScConsoleDebugStream::Flush() {
  std::fflush(stderr);
}

#ifdef _WIN32
ScWindowDebugStream *ScWindowDebugStream::instance = nullptr;

ScChangeFontDebugCommand::ScChangeFontDebugCommand()
    : ScDebugCommandBase(ScString("font"), ScString(" [size] [name]")), stream(nullptr) {
}

// Parse the font command's size and remaining name words, then update its borrowed debug stream.
void ScChangeFontDebugCommand::Execute(const char *text) {
  ScString input(text), name;
  std::vector<ScString> arguments;
  i32 count = ScParseDebugArguments(input, &arguments);
  if (count >= 2) {
    for (i32 i = 2; i < count; ++i) {
      name.Append(arguments[i].ToChar());
      if (i != count - 1)
        name.Append(" ");
    }
    if (stream)
      stream->SetDisplayFont(name, std::atoi(arguments[1].ToChar()));
  }
}

// Construct the Windows debug service without windows or a display font, then publish its singleton.
ScWindowDebugStream::ScWindowDebugStream()
    : ScDebugStreamBase(kScOutputStreamClassId, kScConsoleServicePriority), visible(false), requested_visible(false),
      window(nullptr), edit(nullptr), font(nullptr) {
  instance = this;
}

ScWindowDebugStream::~ScWindowDebugStream() {
  instance = nullptr;
}

// Both control dimensions require a populated desktop work-area rectangle.
bool ScWindowDebugStream::Init() {
  RECT work;
  [[maybe_unused]] BOOL work_valid = SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
  assert(work_valid);
  i32 width = (work.right - work.left) / 3, height = work.bottom - work.top;
  HINSTANCE module = GetModuleHandleA(nullptr);
  WNDCLASSA window_class{};
  window_class.style = CS_OWNDC;
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = module;
  window_class.hIcon = LoadIconA(module, MAKEINTRESOURCEA(1000));
  window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  window_class.lpszClassName = "cGZDebugStreamW95";
  RegisterClassA(&window_class);
  window = CreateWindowExA(0, "cGZDebugStreamW95", "Gonzo Debug Window",
      (requested_visible ? WS_VISIBLE : 0) | kScDebugFrameWindowStyle, work.right - width, work.top, width, height,
      nullptr, nullptr, module, nullptr);
  SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  edit = CreateWindowExA(0, "Edit", "", kScDebugEditWindowStyle, 0, 0, width, 25, window, nullptr, module, nullptr);
  OnSize(0, static_cast<u16>(width), static_cast<u16>(height));
  font_command.stream = this;
  RegisterCommand(&font_command);
  ShowWindow();
  return true;
}

bool ScWindowDebugStream::Shutdown() {
  if (font) {
    DeleteObject(font);
    font = nullptr;
  }
  if (window) {
    DestroyWindow(window);
    window = nullptr;
    edit = nullptr;
  }
  return true;
}

// Append text to the stored lines, split on newlines, and refresh the visible window.
bool ScWindowDebugStream::Write(const char *text) {
  lock.Lock();
  AppendLines(text);
  if (visible) {
    RecalcScrollRange();
    if (window)
      InvalidateRect(window, nullptr, TRUE);
  }
  lock.Unlock();
  return true;
}

// Replace the display font using the requested point size and the window device's vertical resolution.
bool ScWindowDebugStream::SetDisplayFont(const ScString &name, i32 size) {
  HDC dc = GetDC(window);
  bool result = false;
  lock.Lock();
  if (window) {
    if (font) {
      DeleteObject(font);
      font = nullptr;
    }
    if (name.begin != name.end) {
      i32 height = MulDiv(size, GetDeviceCaps(dc, LOGPIXELSY), 72);
      font = CreateFontA(static_cast<i32>(0u - static_cast<u32>(height)), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 1, 1,
          name.begin);
      result = font != nullptr;
    }
  }
  ReleaseDC(window, dc);
  lock.Unlock();
  return result;
}

void ScWindowDebugStream::ShowWindow() {
  visible = requested_visible = true;
  Write("Debug Window Shown.\n");
  if (window)
    ::ShowWindow(window, SW_SHOW);
}

// Write the notice before clearing visibility and hiding an existing debug window.
void ScWindowDebugStream::HideWindow() {
  Write("Debug Window Hidden.\n");
  visible = requested_visible = false;
  if (window)
    ::ShowWindow(window, SW_HIDE);
}

// Resize the edit control to the bottom of the debug window and refresh its scroll range.
// Both rectangle queries run before the control is moved.
void ScWindowDebugStream::OnSize(u32, u16, u16) {
  if (window) {
    RECT client, editor;
    lock.Lock();
    BOOL client_valid [[maybe_unused]] = GetClientRect(window, &client),
        editor_valid [[maybe_unused]] = GetClientRect(edit, &editor);
    assert(client_valid && editor_valid);
    MoveWindow(edit, 0, client.bottom - editor.bottom, client.right, editor.bottom, TRUE);
    RecalcScrollRange();
    InvalidateRect(window, nullptr, TRUE);
    lock.Unlock();
  }
}

// Paint visible stored lines unless the recursive section is already held, in which case only validate the paint.
// Painting requires populated font metrics for row placement.
void ScWindowDebugStream::OnPaint() {
  if (!window)
    return;
  PAINTSTRUCT paint;
  if (lock.IsLocked()) {
    BeginPaint(window, &paint);
    EndPaint(window, &paint);
    return;
  }
  HDC dc = BeginPaint(window, &paint);
  int scroll = GetScrollPos(window, SB_VERT), row = 0;
  HGDIOBJ old_font = nullptr;
  lock.Lock();
  if (font)
    old_font = SelectObject(dc, font);
  TEXTMETRICA metrics;
  [[maybe_unused]] BOOL metrics_valid = GetTextMetricsA(dc, &metrics);
  assert(metrics_valid);
  for (const ScString &line : lines) {
    if (scroll) {
      --scroll;
    } else {
      TextOutA(dc, 0, row * metrics.tmHeight, line.begin, static_cast<int>(line.end - line.begin));
      ++row;
    }
  }
  if (font)
    SelectObject(dc, old_font);
  EndPaint(window, &paint);
  lock.Unlock();
}

// Apply the original line, page or thumb movement and clamp it to the window's scroll range.
// An existing window must supply a populated scroll range before clamping.
void ScWindowDebugStream::OnScroll(i32 request, i32 position) {
  if (window) {
    int minimum, maximum;
    lock.Lock();
    int current = GetScrollPos(window, SB_VERT);
    switch (request) {
    case 0:
      --current;
      break;
    case 1:
      ++current;
      break;
    case 2:
      current -= 10;
      break;
    case 3:
      current += 10;
      break;
    case 4:
    case 5:
      current = position;
      break;
    }
    if (current < 0)
      current = 0;
    [[maybe_unused]] BOOL range_valid = GetScrollRange(window, SB_VERT, &minimum, &maximum);
    assert(range_valid);
    if (current > maximum)
      current = maximum;
    SetScrollPos(window, SB_VERT, current, TRUE);
    InvalidateRect(window, nullptr, TRUE);
    lock.Unlock();
  }
}

// Update the vertical range from the stored line count and the space above the edit control.
// The visible-line quotient requires populated rectangles and a nonzero text height.
void ScWindowDebugStream::RecalcScrollRange() {
  if (!window)
    return;
  HGDIOBJ old_font = nullptr;
  HDC dc = GetDC(window);
  lock.Lock();
  if (font)
    old_font = SelectObject(dc, font);
  TEXTMETRICA metrics;
  [[maybe_unused]] BOOL metrics_valid = GetTextMetricsA(dc, &metrics);
  assert(metrics_valid);
  if (font)
    SelectObject(dc, old_font);
  ReleaseDC(window, dc);
  RECT client, editor;
  BOOL client_valid [[maybe_unused]] = GetClientRect(window, &client),
      editor_valid [[maybe_unused]] = GetClientRect(edit, &editor);
  assert(client_valid && editor_valid && metrics.tmHeight);
  i32 visible_lines = (client.bottom - editor.bottom) / metrics.tmHeight;
  i32 maximum = static_cast<i32>(lines.size()) - visible_lines;
  if (maximum < 0)
    maximum = 0;
  int old_minimum, old_maximum;
  [[maybe_unused]] BOOL range_valid = GetScrollRange(window, SB_VERT, &old_minimum, &old_maximum);
  assert(range_valid);
  int position = GetScrollPos(window, SB_VERT);
  SetScrollRange(window, SB_VERT, 0, maximum, TRUE);
  if (position >= old_maximum)
    SetScrollPos(window, SB_VERT, maximum, TRUE);
  ShowScrollBar(window, SB_VERT, TRUE);
  lock.Unlock();
}

// Dispatch a completed edit line, echo it, clear the editor and refresh the debug window's scroll range.
// The edit control must respect the original 255-character input limit.
void ScWindowDebugStream::OnEditChange() {
  if (!edit)
    return;
  char buffer[256]{};
  buffer[0] = static_cast<char>(255);
  ScString name;
  lock.Lock();
  LRESULT length = SendMessageA(edit, EM_GETLINE, 0, reinterpret_cast<LPARAM>(buffer));
  assert(length >= 0 && length < static_cast<LRESULT>(sizeof(buffer)));
  buffer[length] = 0;
  if (SendMessageA(edit, EM_GETLINECOUNT, 0, 0) > 1) {
    ScString echo(">");
    echo.Append(buffer);
    echo.Append("\n");
    ScString input(buffer);
    std::vector<ScString> arguments;
    i32 count = ParseCommandLine(input, &arguments);
    lock.Lock();
    if (count) {
      name = input;
      ScString help_lower("help"), help_upper("Help");
      if (name.IsEqual(&help_lower) || name.IsEqual(&help_upper)) {
        ScString command_name, usage;
        for (ScDebugCommand *command : commands) {
          command->GetCommandString(&command_name);
          command->GetUsageString(&usage);
          command_name.Append(" ");
          command_name.Append(usage.begin, usage.ScString::Strlen());
          command_name.Append("\n");
          Write(command_name.ToChar());
        }
      } else {
        for (ScDebugCommand *command : commands) {
          int depth = lock.IsLocked();
          command->GetCommandString(&name);
          if (name.IsEqual(&arguments[0])) {
            for (int i = 0; i < depth; ++i)
              lock.Unlock();
            command->Execute(buffer);
            for (int i = 0; i < depth; ++i)
              lock.Lock();
          }
        }
      }
    }
    Write(echo.ToChar());
    SetWindowTextA(edit, "");
    RecalcScrollRange();
    lock.Unlock();
  }
  lock.Unlock();
}

// Return the current borrowed Windows debug-stream singleton.
ScWindowDebugStream *ScWindowDebugStream::Instance() {
  return instance;
}

// Route the owned window's size, focus, paint, edit and vertical-scroll messages to their original handlers.
LRESULT CALLBACK ScWindowDebugStream::WindowProcedure(HWND target, UINT message, WPARAM first, LPARAM second) {
  if (target == instance->window) {
    switch (message) {
    case WM_SIZE:
      Instance()->OnSize(static_cast<u32>(first), LOWORD(second), HIWORD(second));
      return 0;
    case WM_SETFOCUS:
      SetFocus(Instance()->edit);
      return 0;
    case WM_PAINT:
      Instance()->OnPaint();
      return 0;
    case WM_COMMAND:
      if (HIWORD(first) == EN_CHANGE)
        Instance()->OnEditChange();
      return 0;
    case WM_VSCROLL:
      Instance()->OnScroll(LOWORD(first), HIWORD(first));
      return 0;
    }
  }
  return DefWindowProcA(target, message, first, second);
}
#endif

// Copyright (C) 2024, 2026 Nicholas Hayes
// Director declaration/behavior witness: sc3k-gzcom-dll, LGPL-2.1-or-later; installed Unlimited corrections below.
#include "common/sc3_director.h"

struct ScDllClassEntry {
  ScDllClassEntry *next;
  u32 id, reserved;
  ScUnknown *(*factory)();
};

// Retire only registration records. The factories and borrowed directors have independent lifetimes.
ScDllClassTable::~ScDllClassTable() {
  while (first) {
    ScDllClassEntry *entry = first;
    first = entry->next;
    delete entry;
  }
}

ScDllDirector::ScDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

ScDllDirector::~ScDllDirector() {
}

// Publish the primary director or separate hook view before virtual retention. Failed queries clear the output.
bool ScDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScDllDirector::AddRef() {
  return ++references;
}

u32 ScDllDirector::Release() {
  return RemoveRef();
}

// Decrement a nonzero count and retain zero otherwise. This operation does not destroy the director.
u32 ScDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 ScDllDirector::RefCount() {
  return references;
}

// Publish COM before requesting its framework, then copy the path through the original temporary String scope.
// Children receive the original COM/path inputs in order; their return values do not change this result.
bool ScDllDirector::InitializeCOM(ScCom *input_com, ScStringView *path) {
  if (!input_com)
    return false;
  com = input_com;
  framework = input_com->FrameWork();
  {
    ScString copy(*path);
    library_path = copy;
  }
  for (u32 i = 0; i < children.size; ++i)
    children.data[i]->InitializeCOM(input_com, path);
  return true;
}

// Ask children before locating this director's own class. A rejected newly created object is retained then released,
// preserving the original deletion protocol without clearing a failed interface output.
bool ScDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 i = 0; i < children.size; ++i)
    if (children.data[i]->GetClassObject(clsid, iid, out))
      return true;
  for (ScDllClassEntry *entry = classes.first; entry; entry = entry->next)
    if (entry->id == clsid) {
      ScUnknown *object = entry->factory();
      if (object->QueryInterface(iid, out))
        return true;
      object->AddRef();
      object->Release();
      return false;
    }
  return false;
}

// Enumerate child classes first, then this director's ascending class-ID/reserved pairs.
// Stable records preserve insertion observations when the callback extends the class registry.
void ScDllDirector::EnumClassObjects(ScClassCallback callback, void *context) {
  for (u32 i = 0; i < children.size; ++i)
    children.data[i]->EnumClassObjects(callback, context);
  for (ScDllClassEntry *entry = classes.first; entry; entry = entry->next)
    callback(entry->id, entry->reserved, context);
}

bool ScDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Initialize the real common child, recursively flatten its existing descendants, then append the borrowed child.
// The original accesses the shared DLL-director child storage, rather than an economy-specific receiver copy.
void ScDllDirector::AddDirector(ScDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 i = 0; i < director->children.size; ++i)
    AddDirector(director->children.data[i]);
  *children.Append() = director;
}

// Visit children even when this director still has references. Stop at the first refusal, then test the live own count.
bool ScDllDirector::CanUnloadNow() {
  for (u32 i = 0; i < children.size; ++i)
    if (!children.data[i]->CanUnloadNow())
      return false;
  return references == 0;
}

// Register every child without short circuiting. A single refusal makes the final result false.
bool ScDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 i = 0; i < children.size; ++i)
    if (!children.data[i]->AppRegister(registry))
      result = false;
  return result;
}

// Borrow the cached framework published by successful COM initialization.
ScFramework *ScDllDirector::FrameWork() {
  return framework;
}

// Borrow the cached COM pointer. This folded field-load body does not establish unrelated class ownership.
ScCom *ScDllDirector::GZCOM() {
  return com;
}

// The first duplicate pair wins, matching ordered-tree insertion; retail cRZCOMDllDirector::AddCls establishes the void
// return and factory arguments.
void ScDllDirector::AddClass(u32 clsid, ScFactory factory, u32 reserved) {
  ScDllClassEntry **link = &classes.first;
  while (*link && ((*link)->id < clsid || ((*link)->id == clsid && (*link)->reserved < reserved)))
    link = &(*link)->next;
  if (*link && (*link)->id == clsid && (*link)->reserved == reserved)
    return;
  ScDllClassEntry *entry = new ScDllClassEntry{*link, clsid, reserved, factory};
  *link = entry;
  if (!entry->next)
    classes.last = entry;
  ++classes.count;
}

bool ScDllDirector::OnUnload() {
  return true;
}

bool ScDllDirector::PreFrameWorkInit() {
  return true;
}

bool ScDllDirector::PreAppInit() {
  return true;
}

bool ScDllDirector::PostAppInit() {
  return true;
}

bool ScDllDirector::PreAppShutdown() {
  return true;
}

bool ScDllDirector::PostAppShutdown() {
  return true;
}

bool ScDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool ScDllDirector::AbortiveQuit() {
  return true;
}

bool ScDllDirector::OnInstall() {
  return true;
}

#include <SDL3/SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static u32 ScFrameworkFrame, ScFrameworkSlowFrame;

ScExceptionNotification::ScExceptionNotification() : references(0) {
}

ScExceptionNotification::~ScExceptionNotification() {
}

// Publish either supported identity before calling virtual AddRef. Unsupported queries clear the output.
bool ScExceptionNotification::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScExceptionNotification);
  HANDLE_QUERY_INTERFACE(IID_ScExceptionNotification, ScExceptionNotification);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScExceptionNotification::AddRef() {
  return ++references;
}

// Delete on an old count of one without first overwriting that count. Other old counts decrement with DWORD wrapping.
u32 ScExceptionNotification::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Ask whether the pending game exception should be accepted, showing the original bounded module/line diagnostic.
// Only an affirmative dialog result returns true; dialog failure rejects the exception and the text stays terminated.
bool ScExceptionNotification::Notify(const char *module, i32 line) {
  char buffer[200];
  snprintf(buffer, sizeof(buffer), "Module: %s\nLine: %d", module ? module : "not specified", line);
  buffer[199] = 0;
  SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"}};
  SDL_MessageBoxData dialog = {SDL_MESSAGEBOX_INFORMATION, nullptr, "Exception about to be thrown, accept?",
      buffer, 2, buttons, nullptr};
  i32 selected = 0;
  return SDL_ShowMessageBox(&dialog, &selected) && selected == 1;
}

// Construct the original service/hook collections, default interval and concrete command-line owner.
// Mode remains unwritten until startup assigns it; exception notification is allocated and retained last.
ScFrameworkCore::ScFrameworkCore()
    : application(nullptr), idle_interval(4), running(false), installing(false), debug_stream(nullptr),
      output_stream(nullptr), error_stream(nullptr), input_stream(nullptr), exception_notification(nullptr) {
  exception_notification = ScAddRefNotNull(new ScExceptionNotification);
}

// Release notification and lazily owned streams before normal member destruction.
// Concrete command-line, tick/idle lists, ID/priority maps and hook list then retire in the original member order.
ScFrameworkCore::~ScFrameworkCore() {
  if (exception_notification)
    exception_notification->Release();
  if (input_stream)
    input_stream->Release();
  if (error_stream)
    error_stream->Release();
  if (output_stream)
    output_stream->Release();
}

// Register a service in its priority multimap and unique ID map, with the original temporary retaining pairs.
// A duplicate ID returns false after destroying both pairs; equal priorities remain distinct registrations.
bool ScFrameworkCore::AddSystemService(ScSystemService *service) {
  if (!service)
    return false;
  i32 priority = service->GetServicePriority();
  u32 identifier = service->GetServiceID();
  decltype(priority_services)::value_type priority_record(priority, ScFrameworkRef<ScSystemService>(service));
  decltype(identified_services)::value_type id_record(identifier, ScFrameworkRef<ScSystemService>(service));
  if (identified_services.find(identifier) != identified_services.end())
    return false;
  priority_services.insert(priority_record);
  identified_services.insert(id_record);
  return true;
}

// Query the registered service directly without adding a temporary reference. Missing IDs leave output unchanged.
bool ScFrameworkCore::GetSystemService(u32 identifier, u32 iid, void **out) {
  auto entry = identified_services.find(identifier);
  if (entry == identified_services.end())
    return false;
  return entry->second.object->QueryInterface(iid, out);
}

// Enumerate descending priorities, obtaining and releasing each actual Unknown identity around the callback.
// The current reverse iterator advances after the callback, retaining the original live-container traversal.
bool ScFrameworkCore::EnumSystemServices(ScUnknownEnumerator *enumerator, ScUnknown *unknown, u32 iid) {
  ScUnknown *object = nullptr;
  auto end = priority_services.rend();
  for (auto entry = priority_services.rbegin(); entry != end; ++entry)
    if (entry->second.object->QueryInterface(IID_ScUnknown, reinterpret_cast<void **>(&object))) {
      enumerator->DoEnumObject(object, unknown, iid);
      object->Release();
    }
  return true;
}

// Search the captured hook range with a temporary reference, then append only a previously absent identity.
// The search temporary is released before insertion acquires its separate temporary and node references.
bool ScFrameworkCore::AddHook(ScFrameworkHooks *hook) {
  auto entry = hooks.begin(), end = hooks.end();
  {
    ScFrameworkRef<ScFrameworkHooks> search(hook);
    while (entry != end && entry->object != hook)
      ++entry;
  }
  if (entry != end)
    return false;
  hooks.push_back(ScFrameworkRef<ScFrameworkHooks>(hook));
  return true;
}

// Release the argument's search reference before unlinking the matching hook node.
// Only removal of an existing registration succeeds; node destruction releases its independently owned reference.
bool ScFrameworkCore::RemoveHook(ScFrameworkHooks *hook) {
  auto entry = hooks.begin(), end = hooks.end();
  {
    ScFrameworkRef<ScFrameworkHooks> search(hook);
    while (entry != end && entry->object != hook)
      ++entry;
  }
  if (entry == end)
    return false;
  hooks.erase(entry);
  return true;
}

// Preserve the tick registration's two enclosing smart references and its separate search/insertion temporaries.
// Duplicate detection uses the range captured before those retains; insertion reloads the live list.
bool ScFrameworkCore::AddToTick(ScSystemService *service) {
  auto entry = tick_services.begin(), end = tick_services.end();
  ScFrameworkRef<ScSystemService> outer(service), inner(service);
  {
    ScFrameworkRef<ScSystemService> search(service);
    while (entry != end && entry->object != service)
      ++entry;
  }
  if (entry != end)
    return false;
  tick_services.push_back(ScFrameworkRef<ScSystemService>(service));
  return true;
}

// Search the captured tick list before erasing a matching node. Argument and node references are released separately.
bool ScFrameworkCore::RemoveFromTick(ScSystemService *service) {
  auto entry = tick_services.begin(), end = tick_services.end();
  {
    ScFrameworkRef<ScSystemService> search(service);
    while (entry != end && entry->object != service)
      ++entry;
  }
  if (entry == end)
    return false;
  tick_services.erase(entry);
  return true;
}

// Retain for the idle-list search and reject duplicate registration. A new node retains its own service identity.
bool ScFrameworkCore::AddToOnIdle(ScSystemService *service) {
  auto entry = idle_services.begin(), end = idle_services.end();
  {
    ScFrameworkRef<ScSystemService> search(service);
    while (entry != end && entry->object != service)
      ++entry;
  }
  if (entry != end)
    return false;
  idle_services.push_back(ScFrameworkRef<ScSystemService>(service));
  return true;
}

// Finish the temporary idle-list search reference before releasing a removed node's stored reference.
bool ScFrameworkCore::RemoveFromOnIdle(ScSystemService *service) {
  auto entry = idle_services.begin(), end = idle_services.end();
  {
    ScFrameworkRef<ScSystemService> search(service);
    while (entry != end && entry->object != service)
      ++entry;
  }
  if (entry == end)
    return false;
  idle_services.erase(entry);
  return true;
}

i32 ScFrameworkCore::GetOnIdleInterval() {
  return idle_interval;
}

// Dispatch the original fast-list callback at service44. CodeView proves this notification has no return value.
// Ignore callback results rather than manufacturing a result for an empty listener list.
void ScFrameworkCore::OnTick(u32 frame) {
  auto end = tick_services.end();
  for (auto entry = tick_services.begin(); entry != end; ++entry)
    entry->object->OnTick(frame);
}

// Increment the live fast counter before virtual dispatch, then test its changed value against the live interval.
// Divisible frames increment the slow counter and reload it for each original service40 callback.
void ScFrameworkCore::OnIdle() {
  u32 previous = ScFrameworkFrame++;
  OnTick(previous);
  if (ScFrameworkFrame % (u32)idle_interval == 0) {
    ++ScFrameworkSlowFrame;
    auto end = idle_services.end();
    for (auto entry = idle_services.begin(); entry != end; ++entry)
      entry->object->OnIdle(ScFrameworkSlowFrame);
  }
}

// Borrow the embedded concrete command-line identity. Reading it does not retain the framework or command line.
ScCmdLine *ScFrameworkCore::CommandLine() {
  return &command_line;
}

bool ScFrameworkCore::IsInstall() {
  return installing;
}

// The base framework has no debug stream provider. Its stored replacement pointer is not read by this virtual getter.
ScDebugStream *ScFrameworkCore::GetDebugStream() {
  return nullptr;
}

void ScFrameworkCore::SetOnIdleInterval(i32 value) {
  idle_interval = value > 0 ? value : 1;
}

// Return the actual debug-stream identity supplied by virtual lookup. No pointer is narrowed to an integer.
ScDebugStream *ScFrameworkCore::DefaultDebugStream() {
  return GetDebugStream();
}

// Obtain the current debug stream through the same virtual provider. The result is borrowed without retention.
ScDebugStream *ScFrameworkCore::DebugStream() {
  return GetDebugStream();
}

void ScFrameworkCore::SetDebugStream(ScDebugStream *stream) {
  debug_stream = stream;
}

// Lazily create the original null transport with temporary and cached references acquired in order.
// Return the cached stream identity without acquiring a caller reference.
ScOutputStream *ScFrameworkCore::StdOut() {
  if (!output_stream) {
    auto *stream = new ScNullOutputStream;
    stream->AddRef();
    stream->AddRef();
    if (output_stream)
      output_stream->Release();
    output_stream = stream;
    stream->Release();
  }
  return static_cast<ScOutputStream *>(output_stream);
}

// Lazily create the original null transport with temporary and cached references acquired in order.
// Return the cached stream identity without acquiring a caller reference.
ScOutputStream *ScFrameworkCore::StdErr() {
  if (!error_stream) {
    auto *stream = new ScNullOutputStream;
    stream->AddRef();
    stream->AddRef();
    if (error_stream)
      error_stream->Release();
    error_stream = stream;
    stream->Release();
  }
  return static_cast<ScOutputStream *>(error_stream);
}

// Lazily create the original null transport with temporary and cached references acquired in order.
// Return the cached stream identity without acquiring a caller reference.
ScInputStream *ScFrameworkCore::StdIn() {
  if (!input_stream) {
    auto *stream = new ScNullInputStream;
    stream->AddRef();
    stream->AddRef();
    if (input_stream)
      input_stream->Release();
    input_stream = stream;
    stream->Release();
  }
  return static_cast<ScInputStream *>(input_stream);
}

// Query the selected virtual stream provider. Missing output storage and unknown selectors return false.
bool ScFrameworkCore::GetStream(ScStreamType type, u32 iid, void **out) {
  if (!out)
    return false;
  switch (type) {
  case kStreamStdOut:
    return StdOut()->QueryInterface(iid, out);
  case kStreamStdErr:
    return StdErr()->QueryInterface(iid, out);
  case kStreamStdIn:
    return StdIn()->QueryInterface(iid, out);
  default:
    return false;
  }
}

// Release the selected cached owner before querying its replacement. A null replacement clears the owner successfully.
// The original queries the output-stream IID even for standard input; preserve that observable mismatch.
bool ScFrameworkCore::SetStream(ScStreamType type, ScUnknown *stream) {
  ScUnknown **selected;
  switch (type) {
  case kStreamStdOut:
    selected = &output_stream;
    break;
  case kStreamStdErr:
    selected = &error_stream;
    break;
  case kStreamStdIn:
    selected = &input_stream;
    break;
  default:
    return false;
  }
  if (*selected)
    (*selected)->Release();
  *selected = nullptr;
  return !stream || stream->QueryInterface(IID_ScOutputStream, reinterpret_cast<void **>(selected));
}

void ScFrameworkCore::SetApplication(ApApp *value) {
  application = value;
}

// Borrow the application's actual interface without a retain. Startup and shutdown own its lifetime separately.
ApApp *ScFrameworkCore::Application() {
  return application;
}

// Borrow the owned exception-notification object. Its reference is released only by framework destruction.
ScExceptionNotification *ScFrameworkCore::ExceptionNotificationObj() {
  return exception_notification;
}

void ScFrameworkCore::SetConsoleMode(bool value) {
  mode = value;
}

// CodeView proves a void setter.
void ScFrameworkCore::SetCommandLine(ScCommandLine *source) {
  command_line = *source;
  ScString install("-Install");
  installing = command_line.Find(install, true) != -1;
}

// Visit registered pre-framework hooks in their live list order. Individual callback results do not stop traversal.
bool ScFrameworkCore::HookPreFrameWorkInit() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PreFrameWorkInit();
  return 1;
}

// Deliver the pre-application phase without copying the listener list. The next link is read after each callback.
bool ScFrameworkCore::HookPreAppInit() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PreAppInit();
  return 1;
}

// Deliver post-application initialization to each current hook. The framework ignores individual Boolean results.
bool ScFrameworkCore::HookPostAppInit() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PostAppInit();
  return 1;
}

// Notify the live hook list before application shutdown. The method succeeds after every defined callback returns.
bool ScFrameworkCore::HookPreAppShutdown() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PreAppShutdown();
  return 1;
}

// Notify hooks after application shutdown, preserving registrations appended during earlier callbacks.
bool ScFrameworkCore::HookPostAppShutdown() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PostAppShutdown();
  return 1;
}

// Deliver the final service-shutdown notification before the framework clears its remaining collections.
bool ScFrameworkCore::HookPostSystemServiceShutdown() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->PostSystemServiceShutdown();
  return 1;
}

// Deliver abortive-quit hooks before the platform's immediate process exit. Callback results are not vetoes.
bool ScFrameworkCore::HookAbortiveQuit() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->AbortiveQuit();
  return 1;
}

// Run installation callbacks in their current list order. This phase ignores their individual return values.
bool ScFrameworkCore::HookOnInstall() {
  auto end = hooks.end();
  for (auto entry = hooks.begin(); entry != end; ++entry)
    entry->object->OnInstall();
  return 1;
}

// Fetch the application even during installation, registering its primary service only in ordinary startup.
// The typed cross-cast replaces the original concrete class's fixed24-byte secondary-interface adjustment.
bool ScFrameworkCore::PreAppInit() {
  ApApp *app = Application();
  if (!installing)
    AddSystemService(dynamic_cast<ScSystemService *>(app));
  auto end = priority_services.rend();
  for (auto entry = priority_services.rbegin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    if (service->GetServicePriority() <= kScAppInitHighestPriority)
      break;
    if (service->Init())
      service->SetServiceRunning(true);
  }
  return 1;
}

// Initialize the inclusive -2999999..-2000000 priority band, descending from the live registry's highest priority.
// Higher-priority services are skipped; a successful Init marks only that service running.
bool ScFrameworkCore::AppInit() {
  auto end = priority_services.rend();
  for (auto entry = priority_services.rbegin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    i32 priority = service->GetServicePriority();
    if (priority <= kScAppInitHighestPriority) {
      if (priority < kScAppInitLowestPriority)
        return 1;
      if (service->Init())
        service->SetServiceRunning(true);
    }
  }
  return 1;
}

// Initialize services below -2999999 in the original descending traversal. Failed initialization leaves running unchanged.
bool ScFrameworkCore::PostAppInit() {
  auto end = priority_services.rend();
  for (auto entry = priority_services.rbegin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    if (service->GetServicePriority() < kScAppInitLowestPriority && service->Init())
      service->SetServiceRunning(true);
  }
  return 1;
}

// Shut down priorities below -2999999 in ascending order, clearing running only for successful shutdowns.
// Complete the original range before throwing the real framework exception if any shutdown failed.
bool ScFrameworkCore::PreAppShutdown() {
  bool failed = false;
  auto end = priority_services.end();
  for (auto entry = priority_services.begin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    if (service->GetServicePriority() >= kScAppInitLowestPriority)
      break;
    if (service->Shutdown())
      service->SetServiceRunning(false);
    else
      failed = true;
  }
  if (failed)
    throw ScFrameworkException();
  return 1;
}

// Shut down the inclusive system-priority band in ascending registry order, retaining each live priority check.
// Failures accumulate through the range and then raise the original game-specific exception.
bool ScFrameworkCore::AppShutdown() {
  bool failed = false;
  auto end = priority_services.end();
  for (auto entry = priority_services.begin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    i32 priority = service->GetServicePriority();
    if (priority >= kScAppInitLowestPriority) {
      if (priority > kScAppInitHighestPriority)
        break;
      if (service->Shutdown())
        service->SetServiceRunning(false);
      else
        failed = true;
    }
  }
  if (failed)
    throw ScFrameworkException();
  return 1;
}

// Shut down application-priority services above -2000000 after the lower bands have finished.
// Preserve all successful running-state writes before throwing for any failure in this pass.
bool ScFrameworkCore::PostAppShutdown() {
  bool failed = false;
  auto end = priority_services.end();
  for (auto entry = priority_services.begin(); entry != end; ++entry) {
    ScSystemService *service = entry->second.object;
    if (service->GetServicePriority() > kScAppInitHighestPriority) {
      if (service->Shutdown())
        service->SetServiceRunning(false);
      else
        failed = true;
    }
  }
  if (failed)
    throw ScFrameworkException();
  return 1;
}

// Deliver installation notifications in ascending priority order, advancing after each callback.
void ScFrameworkCore::OnInstall() {
  auto end = priority_services.end();
  for (auto entry = priority_services.begin(); entry != end; ++entry)
    entry->second.object->OnInstall();
}

bool ScFrameworkCore::HookRelease() {
  hooks.clear();
  return 1;
}

// Clear idle and tick registrations, then ID and priority maps, releasing each collection's independent references.
// Preserve this explicit shutdown order rather than relying on the framework destructor to perform it later.
bool ScFrameworkCore::SystemServiceRelease() {
  idle_services.clear();
  tick_services.clear();
  identified_services.clear();
  priority_services.clear();
  return 1;
}

// Original process exit-code global SC3U4FAB70; Quit and startup failures share this actual state.
i32 ScApplicationExitCode;
ScFrameworkCore *ScCurrentFramework, *ScAllocatedFramework;
ApBaseApp *(*ApApplicationFactory)();
void (*ApApplicationDestroy)(ApBaseApp *application);

// Borrow the current process framework identity. Startup publication and shutdown clearing own this global state.
ScFrameworkCore *ScFrameworkCore::FrameWork() {
  return ScCurrentFramework;
}

// Run and retire the framework only after successful initialization, then return the shared process exit code.
i32 ScFrameworkCore::Main(ScCommandLine *command_line, bool mode) {
  if (SInit(command_line, mode)) {
    SRun();
    SShutdown();
  }
  return ScApplicationExitCode;
}

// Publish the platform framework before its COM service and application, then deliver the original startup phases.
// Ordinary COM initialization refusal does not abort startup; application/pre-service refusal sets the process exit code.
bool ScFrameworkCore::SInit(ScCommandLine *command_line, bool mode) {
  ScAllocatedFramework = static_cast<ScFrameworkCore *>(CreateFrameWork());
  ScCurrentFramework = ScAllocatedFramework;
  ScAllocatedFramework->AddRef();
  ScCurrentFramework->SetCommandLine(command_line);
  ScCurrentFramework->SetConsoleMode(mode);
  ScCom *com = ScCurrentFramework->GetCOMObject();
  if (com->RealInit())
    com->SetServiceRunning(true);
  void *service_view = nullptr;
  if (com->QueryInterface(IID_ScSystemService, &service_view)) {
    ScSystemService *service = static_cast<ScSystemService *>(service_view);
    ScCurrentFramework->AddSystemService(service);
    service->Release();
    service_view = nullptr;
  }
  assert(ApApplicationFactory);
  ApApp *app = static_cast<ApApp *>(ApApplicationFactory());
  app->AddRef();
  ScCurrentFramework->SetApplication(app);
  app = ScCurrentFramework->Application();
  app->AddApplicationServicesHere();
  app->AddCOMDirectorsHere();
  app->AddDynamicLibrariesHere();
  if (!app->PreFrameWorkInit()) {
    ScApplicationExitCode = -1;
    return false;
  }
  ScCurrentFramework->HookPreFrameWorkInit();
  ScCurrentFramework->PreAppInit();
  ScCurrentFramework->HookPreAppInit();
  if (!ScCurrentFramework->AppInit()) {
    ScApplicationExitCode = -1;
    return false;
  }
  ScCurrentFramework->running = true;
  ScCurrentFramework->PostAppInit();
  app->PostFrameWorkInit();
  ScCurrentFramework->HookPostAppInit();
  for (i32 i = 0; i < command_line->argc(); ++i)
    ;
  if (ScCurrentFramework->installing) {
    ScCurrentFramework->OnInstall();
    ScCurrentFramework->HookOnInstall();
    ShutdownFrameWorkHelper();
  }
  return ScApplicationExitCode == 0;
}

// Let the retained application run first, entering the framework loop only when it declines to handle execution.
bool ScFrameworkCore::SRun() {
  ApApp *app = ScCurrentFramework->Application();
  if (!app->Run())
    ScCurrentFramework->Run();
  return ScApplicationExitCode == 0;
}

// Capture the application once, but reload the process framework after callbacks exactly as the source does.
bool ScFrameworkCore::SShutdown() {
  ApApp *app = ScCurrentFramework->Application();
  ScCurrentFramework->HookPreAppShutdown();
  app->PreFrameWorkShutdown();
  ScCurrentFramework->PreAppShutdown();
  ScCurrentFramework->AppShutdown();
  ScCurrentFramework->HookPostAppShutdown();
  ScCurrentFramework->PostAppShutdown();
  ScCurrentFramework->running = false;
  ScCurrentFramework->HookPostSystemServiceShutdown();
  ScCurrentFramework->SystemServiceRelease();
  ScCurrentFramework->HookRelease();
  ScCurrentFramework->ShutdownGZCOM();
  app->Release();
  ShutdownFrameWorkHelper();
  ScCurrentFramework = nullptr;
  ScAllocatedFramework = nullptr;
  return ScApplicationExitCode == 0;
}

void ScFrameworkCore::DestroyFrameWork(ScFramework *framework) {
  if (framework)
    delete static_cast<ScFrameworkCore *>(framework);
}

// Invoke the optional application destructor hook before deleting the live framework, then clear the process pointer.
// The callback receives the application's actual primary base; reload the framework after callback reentry.
void ScFrameworkCore::ShutdownFrameWorkHelper() {
  if (ScCurrentFramework) {
    if (ApApplicationDestroy)
      ApApplicationDestroy(static_cast<ApBaseApp *>(ScCurrentFramework->Application()));
    DestroyFrameWork(ScCurrentFramework);
    ScCurrentFramework = nullptr;
  }
}
