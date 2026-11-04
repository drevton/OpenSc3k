#ifndef SC3_COMMON_SC3_DEBUG_STREAM_H
#define SC3_COMMON_SC3_DEBUG_STREAM_H

#include <deque>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/com.h"
#include "common/sc3_stream.h"

// The original command interface is a three-method object, not a reference-counted COM identity.
class ScDebugCommand {
public:
  virtual void GetCommandString(ScStringView *out) = 0;
  virtual void GetUsageString(ScStringView *out) = 0;
  virtual void Execute(const char *arguments) = 0;
};

// Original cRZDebugCommand owns its command and usage strings.
class ScDebugCommandBase : public ScDebugCommand {
public:
  ScDebugCommandBase(const ScString &command, const ScString &usage);
  ~ScDebugCommandBase();
  void GetCommandString(ScStringView *out) override;
  void GetUsageString(ScStringView *out) override;
  void Execute(const char *arguments) override;

protected:
  ScString command, usage;
};

// Installed cIGZDebugStream table at 004DA85C.
class ScDebugStream : public ScUnknown {
public:
  virtual bool Write(const ScStringView &text) = 0;
  virtual bool Write(const char *text) = 0;
  virtual ScOutputStream *operator<<(const ScStringView &text) = 0;
  virtual ScOutputStream *operator<<(const char *text) = 0;
  virtual void RegisterCommand(ScDebugCommand *command) = 0;
  virtual void UnregisterCommand(ScDebugCommand *command) = 0;
  virtual void ExecuteCommand(ScStringView *command) = 0;
  virtual bool SaveToFile(const ScStringView &path) = 0;
};

// Installed cIGZDebugCommandService secondary identity at 004DA844.
class ScDebugCommandService : public ScUnknown {
public:
  virtual void RegisterCommand(ScDebugCommand *command) = 0;
  virtual void UnregisterCommand(ScDebugCommand *command) = 0;
  virtual void ExecuteCommand(ScStringView *command) = 0;
};

// cGZDebugStream uses deque<cRZString> and vector<cIGZDebugCommand *> in retail CodeView.
// Its transport, command interpreter and remaining undefined numeric formatting paths remain abstract.
class ScDebugStreamBase : public ScServiceBase, public ScDebugStream, public ScDebugCommandService,
                          public ScTextOutputStream, public ScThreadSafe {
public:
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Write(const ScStringView &text) override;
  using ScDebugStream::Write;
  ScOutputStream *operator<<(const ScStringView &text) override;
  ScOutputStream *operator<<(const char *text) override;
  void Flush() override;
  void SetRZCharStr(const char *text) override;
  void RegisterCommand(ScDebugCommand *command) override;
  void UnregisterCommand(ScDebugCommand *command) override;
  void ExecuteCommand(ScStringView *command) override;
  bool SaveToFile(const ScStringView &path) override;
  virtual void DisplayCommand(const char *text);
  virtual i32 ParseCommandLine(const ScString &text, std::vector<ScString> *arguments);

protected:
  ScDebugStreamBase(u32 identifier, i32 priority);
  ~ScDebugStreamBase() override;
  void AppendLines(const char *text);
  std::deque<ScString> lines;
  std::vector<ScDebugCommand *> commands;
};

// Native portable transport for the recovered debug/command interfaces; it writes real process stderr.
class ScConsoleDebugStream : public ScDebugStreamBase {
public:
  ScConsoleDebugStream();
  using ScDebugStreamBase::Write;
  bool Write(const char *text) override;
  void Flush() override;
};

#ifdef _WIN32
class ScWindowDebugStream;

// The original font command is embedded in the Windows debug stream and borrows its owner.
class ScChangeFontDebugCommand : public ScDebugCommandBase {
public:
  ScChangeFontDebugCommand();
  void Execute(const char *arguments) override;
  ScWindowDebugStream *stream;
};

// The Windows service owns its two window handles, font and embedded font command.
// Unrecovered transport, command and painting operations remain explicit declarations.
class ScWindowDebugStream : public ScDebugStreamBase {
public:
  ScWindowDebugStream();
  ~ScWindowDebugStream() override;
  bool Init() override;
  bool Shutdown() override;
  bool Write(const char *text) override;
  using ScDebugStreamBase::Write;
  virtual bool SetDisplayFont(const ScString &name, i32 size);
  virtual void ShowWindow();
  virtual void HideWindow();
  static ScWindowDebugStream *Instance();

protected:
  void OnSize(u32 type, u16 width, u16 height);
  void OnPaint();
  void OnScroll(i32 request, i32 position);
  void RecalcScrollRange();
  void OnEditChange();
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM first, LPARAM second);
  static ScWindowDebugStream *instance;
  bool visible, requested_visible;
  HWND window, edit;
  HFONT font;
  ScChangeFontDebugCommand font_command;
};
#endif

#endif
