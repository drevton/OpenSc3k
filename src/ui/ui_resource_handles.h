#ifndef SC3_UI_UI_RESOURCE_HANDLES_H
#define SC3_UI_UI_RESOURCE_HANDLES_H

#include "graphics/image_codecs.h"
#include "resources/api.h"

// The original scoped image handle owns the queried buffer and releases it when the handle leaves scope.
class UiImageHandle {
public:
  UiImageHandle();
  UiImageHandle(const UiImageHandle &source);
  UiImageHandle(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~UiImageHandle();
  UiImageHandle &operator=(const UiImageHandle &source);
  GxBuffer *GetBuffer() const;
  bool IsBufferPresent() const;

protected:
  void Load(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

class FtFont;
class UiTokenData;
struct UiTokenCallbackContext;
using UiTokenCallback = bool (*)(
    const ScString &text, i32 position, UiTokenData *token, UiTokenCallbackContext *context);

// The scoped text reference owns its String interface and retains the original complete resource key.
class UiTextHandle {
public:
  static void FormatForEllipsis(ScString *text, i32 width, FtFont *font);
  UiTextHandle();
  UiTextHandle(const UiTextHandle &source);
  UiTextHandle(u32 instance, u32 group);
  UiTextHandle &operator=(const UiTextHandle &source);
  bool operator==(const ScStringView &other);
  operator ScString();
  const RsKey *GetResKey() const;
  void GetResKey(RsKey *out) const;
  bool IsStringPresent();
  ScString Val();
  bool Detokenize(ScStringView *out);
  bool Detokenize(ScStringView *out, UiTokenData **tokens, u32 count);
  bool Detokenize(ScStringView *out, UiTokenCallback callback, UiTokenCallbackContext *context);
  bool ReplaceParameter(ScString *name, ScString *replacement);
  bool ReplaceParameter(ScString *name, ScString *replacement, ScString *out);
  virtual ~UiTextHandle();
  ScStringView *GetText();

protected:
  void Resolve();
  void UnloadResource();
  void ReplaceParameters(ScString *value);
  void ReplaceParameters(ScString *value, UiTokenData **tokens, u32 count);
  void ReplaceParameters(ScString *value, UiTokenCallback callback, UiTokenCallbackContext *context);
  bool ReplaceCommonParameter(ScString *value, i32 *position);
  bool ReplaceParameter(ScString *value, const ScString &name, i32 *position);
  bool ReplaceParameter(ScString *value, UiTokenData *token, i32 *position);
  ScStringView *text;
  RsKey key;
};

class UiMenuLabel;
RsManager *ApGetResourceManager();
bool UiCreateMenuLabel(u32 identifier, u32 group, u32 instance, UiMenuLabel **out);
bool UiLoadMenuImage(u32 group, u32 instance, u32 color, GxBuffer **out);

#endif
