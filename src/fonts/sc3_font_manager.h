#ifndef SC3_FONTS_SC3_FONT_MANAGER_H
#define SC3_FONTS_SC3_FONT_MANAGER_H

#include <map>

#include "common/api.h"
#include "fonts/api.h"
#include "fonts/sc3_bitmap_font.h"

#ifdef __cplusplus
struct FtNode {
  FtNode *next, *previous;
  FtFont *font;
};

// Actual font-cache ownership and linked-list traversal, without comparison pinning or callback services.
struct FtManager {
  FtNode head;
  u32 count;
  bool initialized;
  FtManager();
  ~FtManager();
  FtFont *Load(ScStringView *path);
  u32 Remove(FtFont *font);
  bool Close();
  void Insert(FtFont *font);
};

// The original cache owns one sentinel pointer; its sentinel and font nodes are retired separately from the name tree.
struct FtSystemList {
  FtSystemList();
  ~FtSystemList();
  FtNode *head;
};

// Original primary font-system facet and secondary system-service facet.
class FtSystem : public FtFontSystem, public ScSystemService {
public:
  FtSystem();
  virtual ~FtSystem();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  FtFont *FontAcquire(FtType type, u32 size, u32 flags, u32 color) override;
  FtFont *FontAcquire(const ScStringView &filename) override;
  u32 FontRelease(FtFont *font) override;
  u8 FontSetName(FtType type, const ScStringView &name) override;
  u8 FontGetName(FtType type, ScStringView *name) override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 value) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool value) override;
  bool OnIdle(u32 value) override;
  bool OnTick(u32 value) override;
  bool OnInstall() override;

private:
  u32 service_id;
  bool initialized;
  FtSystemList fonts;
  std::map<u32, ScString> names;
  u32 references;
};

extern u32 FtLocale;
#endif

#endif
