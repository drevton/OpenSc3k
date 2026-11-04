#ifndef SC3_CITY_INITIALIZATION_H
#define SC3_CITY_INITIALIZATION_H

#include <list>
#include <map>
#include <utility>
#include <vector>

#include "city/api.h"
#include "city/city.h"
#include "city/sc_random.h"
#include "common/sc3.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "graphics/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"

class CtAgentType;
class ScStringView;
class CtAgentTreeImpl;

// Original registry callbacks populate agent nodes and hierarchy links; their interface dependencies are unresolved.
void SC3_CDECL CtReadInitAgentType(const ScStringView &key, const ScStringView &value, void *context);
void SC3_CDECL CtReadInitAgentHierarchy(const ScStringView &key, const ScStringView &value, void *context);

// Installed cached ancestry key stores descendant first and ancestor second in lexicographic order.
struct CtAncestorKey {
  u32 descendant, ancestor;
  bool operator<(const CtAncestorKey &other) const;
};

// The original cache distinguishes an untested key from a found or absent ancestor.
enum CtAncestorState : u8 {
  kCtAncestorUnknown = 0,
  kCtAncestorFound = 1,
  kCtAncestorMissing = 2,
};

// Original cached result contains a byte state and a full-width distance.
struct CtAncestorResult {
  u8 state = kCtAncestorUnknown;
  u32 distance = ~u32(0);
};

// SIMINIT's agent-tree service retains registered agent types in its original ordered maps.
class CtAgentTreeImpl final : public CtAgentTree {
public:
  CtAgentTreeImpl();
  ~CtAgentTreeImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  u32 Count() override;
  CtAgentType *GetRootNode() override;
  CtAgentType *GetNode(u32 id) override;
  bool IsAncestor(u32 ancestor, u32 descendant) override;
  bool IsAncestor(u32 ancestor, u32 descendant, u32 *distance) override;
  bool CreateNodeIterator(CtAgentTreeIterator **out) override;
  void DebugDumpTree(CtAgentType *node) override;

private:
  friend class CtAgentTreeIteratorImpl;
  friend void SC3_CDECL CtReadInitAgentType(const ScStringView &, const ScStringView &, void *);
  friend void SC3_CDECL CtReadInitAgentHierarchy(const ScStringView &, const ScStringView &, void *);
  bool AddNode(CtAgentType *node);
  bool LinkNodes(u32 child, u32 parent);
  bool initialized;
  CtAgentType *root;
  u32 references;
  std::map<u32, CtAgentType *> nodes;
  std::map<CtAncestorKey, CtAncestorResult> ancestry;
};

// The iterator borrows its tree and walks the retained type map without retaining individual values.
class CtAgentTreeIteratorImpl final : public CtAgentTreeIterator {
public:
  explicit CtAgentTreeIteratorImpl(CtAgentTreeImpl *tree);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  CtAgentType *Next() override;

private:
  CtAgentTreeImpl *tree;
  std::map<u32, CtAgentType *>::const_iterator current, end;
  u32 references;
};

class ScString;
class ScStringView;
class CtNewCityInfo;
class RsDatabaseSegment;

// SIMINIT commands retain the owning module through the shared command lifetime operations.
class CtInitCommand : public UiCommand {
public:
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
};

// SIMINIT's shutdown-city command uses the common game-command base.
class CtInitShutdownCommand : public CtInitCommand {
public:
  bool Execute() override;
};

// The registered dialog command selects a modal window from its input and publishes its result.
class CtInitDialogCommand : public CtInitCommand {
public:
  CtInitDialogCommand();
  bool Execute() override;

protected:
  // Original slot 56 returns a full-width zero or one; nominal source Boolean/integer spelling remains unresolved.
  virtual u32 PublishResult(u8 accepted, const ScStringView *path);
};

// Save command identity is established by the application command factory and retains the installed transaction.
class CtInitSaveCommand : public CtInitCommand {
public:
  CtInitSaveCommand();
  bool Execute() override;

protected:
  // Original virtual slot 14; byte return is observed as 0 or 1, and the nominal source type remains unverified.
  // The operation reports observed success or failure after selection and the file transaction.
  virtual u8 SavePath(ScString *path);
};

// Load command retains the original request kind and dispatches the selected file to the appropriate loader.
class CtInitLoadCommand : public CtInitCommand {
public:
  CtInitLoadCommand();
  bool Execute() override;

protected:
  bool ReadParameters(u32 *kind, u8 *confirm, u8 *showDialog, u8 *special, ScString *path);
  bool SelectFile(ScString *path);
  bool ConfirmSave();
  bool ApplyCityScheme(RsDatabaseSegment *segment);
  // The three loaders' source-level Boolean/BYTE spelling remains unresolved; retain their observed AL values.
  u8 LoadCity(ScString *path, CtNewCityInfo *settings);
  u8 LoadGenerated(ScString *path, u8 terrain);
  u8 LoadScenario(ScString *path);
  u32 requestKind;
};

// The new-city command has no additional constructed fields.
class CtInitNewCityCommand : public CtInitCommand {
public:
  CtInitNewCityCommand();
  bool Execute() override;

protected:
  virtual bool CreateCity(CtNewCityInfo *settings);
};

// SIMINIT's command-factory base owns the installed vector of retained child factories.
class CtInitFactoryBase : public ScCommandFactory {
public:
  CtInitFactoryBase();
  virtual ~CtInitFactoryBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 classId, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual bool CreateCommand(u32 classId, u32 iid, void **out) = 0;

private:
  u32 references;
  std::vector<ScCommandFactory *> children;
};

// The initialization module's concrete command factory supplies the final local creation operation.
class CtInitCommandFactory : public CtInitFactoryBase {
public:
  bool CreateCommand(u32 classId, u32 iid, void **out) override;
};

class CtCity;

// SIMINIT's city-definition receiver owns the agent tree, occupant cache and statically initialized city.
class CtDefinitionImpl : public CtDefinition {
public:
  CtDefinitionImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  CtAgentTree *GetAgentTypeTree() override;
  OcAttribCache *GetOccupantAttribCache() override;
  bool RegisterLayer(u32 first, u32 second, u32 third) override;
  u32 GetFileType(const ScStringView &path) override;
  bool ValidateSC2File(const ScStringView &path) override;
  bool ValidateSCXFile(const ScStringView &path) override;
  bool ValidateSC3File(const ScStringView &path) override;
  bool ValidateSNRFile(const ScStringView &path) override;
  virtual ~CtDefinitionImpl();
  virtual bool ValidateSCTFile(const ScStringView &path);
  virtual bool ValidateST3File(const ScStringView &path);

private:
  bool initialized;
  u32 references;
  CtAgentTree *agentTree;
  OcAttribCache *attributes;
  CtCity *city;
};

// SIMINIT's process-lifetime director registers the city's initialization classes.
class CtInitDirector : public ScDllDirector {
public:
  CtInitDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

ScDirector *CtGetInitDirector();

bool SC3_CDECL CtInitIsBlankFileName(const char *text);

class FtFont;
class GxBuffer;
class RsManager;
class RsDirectoryListing;
class CtDate;

// The dialog's scoped image resource owns the buffer returned by the real resource manager.
class CtInitImageResource {
public:
  CtInitImageResource();
  CtInitImageResource(const CtInitImageResource &source);
  CtInitImageResource(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format);
  virtual ~CtInitImageResource();
  CtInitImageResource *operator=(const CtInitImageResource &source);
  GxBuffer *Value() const;
  bool IsBufferPresent() const;

private:
  void LoadResource(RsManager *manager, u32 group, u32 instance);
  GxBuffer *buffer;
};

// Each original 40-byte selection record owns a pair of strings.
struct CtInitFilePair {
  ScString first, second;
};

// The city initialization command constructs this callback for the modal city-file selector.
// The callback owns the selected path, preview state and directory rows for the modal dialog.
class CtInitFileDialog : public UiWindowCallback {
public:
  explicit CtInitFileDialog(u32 mode);
  virtual ~CtInitFileDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool BuildWindow(WnWindow **out);
  bool AddImageButton(WnWindow *parent, u32 identifier, i32 x, i32 y, u32 image, i8 key);
  bool Close(WnWindow *window);
  bool GetLimits(WnWindow *window, ScRect *limits);
  // The installed return is forwarded from GZWinMsgPost; its private nominal Boolean/BYTE type remains unresolved.
  u8 Finish(WnWindow *window, u32 result);
  u8 UpdateDirectoryFromCombo(WnWindow *window);
  i32 FindKind(ScString *name);
  bool FindName(i32 kind, ScString *out);

private:
  friend class CtInitDialogCommand;
  // These dialog operations return observed byte results; their nominal Boolean/BYTE spelling is unresolved.
  u8 BuildContent(WnWindow *window);
  u8 BuildDriveCombo(WnWindow *window, FtFont *font);
  u8 Commit(WnWindow *window, u32 identifier);
  u8 Populate(WnWindow *window);
  void UpdatePreview(WnWindow *window);
  // The installed private return is unconsumed; callers use only the supplied outputs.
  void ReadCityInfo(const ScString &path, i32 *population, i64 *funds, CtDate *date,
      ScString *mayor, ScString *city, GxBuffer **preview);
  void UpdateCityLabels(WnWindow *window, const ScString *mayor, const ScString *city);
  u8 RefreshDirectory(WnWindow *window);
  std::list<ScUnknown *> entries;
  u32 mode, references;
  ScString output, first, second, third;
  RsDirectoryListing *directoryListing;
  ScString fourth, fifth, sixth, seventh, eighth;
  u8 flags[4];
  std::vector<CtInitFilePair> selections;
  std::list<ScString> directories;
  std::list<ScString>::iterator currentDirectory;
};

class ApGameInterface;
class CtCity;
class CtAgentTree;
class OcAttribCache;
class ScCommandFactory;
class ScFramework;
class ScMessageServer;
class ScRegistry;
class RsFileSystem;
class RsManager;
class GxGraphics;
class WnWindowManager;
class WnResources;
class ScLanguageManager;
class ApBoxX;
class ApPreferences;
class RsImageOptions;
class SpImageCache;

// The original automatic-cursor scope is shared by load, save and file-preview operations.
class CtInitCursorScope {
public:
  explicit CtInitCursorScope(u32 identifier);
  ~CtInitCursorScope();

private:
  u8 pushed;
};

ScFramework *CtGetInitFramework();
ApGameInterface *CtGetInitApplication();
CtCity *CtGetInitCity();
int CtInitHasCity();
ScCommandFactory *CtGetInitCommandFactory();
ScMessageServer *CtGetInitMessageServer();
ScRegistry *CtGetInitRegistry();
CtAgentTree *CtGetInitAgentTree();
RsManager *CtGetInitResourceManager();
RsFileSystem *CtGetInitFileSystem();
OcAttribCache *CtGetInitAttribCache();
GxGraphics *CtGetInitGraphics();
WnWindowManager *CtGetInitWindowManager();
WnResources *CtGetInitWindowResources();
ScLanguageManager *CtGetInitLanguageManager();
ApBoxX *CtGetInitBox();
RsImageOptions *CtGetInitImageFactory();
ApPreferences *CtGetInitPreferences();
SpImageCache *CtGetInitImageCache();

// The installed resource-key map compares group, then instance, then type.
struct OcInitKeyLess {
  bool operator()(const RsKey &left, const RsKey &right) const;
};

// Each installed searchable attribute record contains a key and four additional words.
struct OcInitAttribRecord {
  RsKey key;
  u32 type, unknown, sizeX, sizeZ;
};

using OcInitAttribMap = std::map<RsKey, GeAttrib *, OcInitKeyLess>;
using OcInitSizeKey = std::pair<u32, u32>;
using OcInitSizeMap = std::map<OcInitSizeKey, std::vector<u32> *>;
using OcInitBuildingIndex = std::map<u32, OcInitSizeMap *>;
class OcInitCacheIterator;
class OcInitAttribTest;
class ScString;
void SC3_CDECL OcReadInitAttribKey(const RsKey &key, void *context);
void SC3_CDECL OcReadInitRegistryAttrib(const ScStringView &key, const ScStringView &value, void *context);
void SC3_CDECL OcReadInitSegment(ScString *key, ScString *value, ScUnknown *owner);
void SC3_CDECL OcReadInitSegmentKey(const RsKey &key, void *context);

// Installed occupant-attribute cache and its keyed records and building-size index.
class OcInitAttribCache : public OcAttribCache {
public:
  OcInitAttribCache();
  virtual ~OcInitAttribCache();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtDefinition *definition) override;
  bool Shutdown() override;
  bool GetOccupantAttrib(const RsKey &key, GeAttrib **out) override;
  u8 GetBuildingAttrib(const RsKey &key, GeBuildingType **out) override;
  bool GetBuildingAttrib(u32 type, GeBuildingType **out) override;
  i32 Count() override;
  bool GetGenericAttrib(const RsKey &key, u32 iid, void **out) override;
  bool GetABuildingOfType(u32 type, u32 size, RsKey *out) override;
  OcCacheIterator *CreateNodeIterator() override;
  void DeleteNodeIterator(OcCacheIterator *iterator) override;
  void AddAttrib(const RsKey &key) override;
  bool AddAttribs(const RsKeyListInterface &keys) override;
  void PostOccAttribChangedMessage() override;
  u8 ModifyOccupantAttrib(const RsKey &key, OcOverride *replacement) override;
  bool ResetAllModifiedAttribs() override;

private:
  friend class OcInitCacheIterator;
  friend void SC3_CDECL OcReadInitAttribKey(const RsKey &, void *);
  friend void SC3_CDECL OcReadInitRegistryAttrib(const ScStringView &, const ScStringView &, void *);
  friend void SC3_CDECL OcReadInitSegment(const ScString &, const ScString &, void *);
  friend void SC3_CDECL OcReadInitSegmentKey(const RsKey &, void *);
  GeAttrib *FindAttrib(const RsKey &key);
  bool AddAttribInternal(const RsKey &key, bool rebuild);
  // Updates affected city occupants when a cached attribute is replaced.
  void ReplaceAttribute(const RsKey &key, GeAttrib *oldAttribute, GeAttrib *newAttribute);
  void CollectMatchingOccupants(
      ScOccupantManager *manager, OcInitAttribTest *filter, std::vector<ScOccupant *> *occupants);
  u8 LoadSegments();
  u32 RebuildBuildingIndex();
  u8 ClearBuildingIndex();
  bool ModifyAttribInternal(GeAttrib *attribute, OcOverride *replacement);
  bool initialized;
  u32 references;
  OcInitAttribTest *test;
  u8 state;
  CtDefinition *definition;
  CtAgentTree *agentTree;
  OcInitAttribMap attributes;
  std::vector<OcInitAttribRecord> records;
  OcInitBuildingIndex buildings;
  std::vector<u32> segments;
  ScRandom random;
};

// The iterator borrows the cache and walks its ordered map without retaining returned attributes.
class OcInitCacheIterator final : public OcCacheIterator {
public:
  explicit OcInitCacheIterator(OcInitAttribCache *cache);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  GeAttrib *GetNext() override;

private:
  OcInitAttribCache *cache;
  OcInitAttribMap::const_iterator current, end;
  u32 references;
};

// The cache's occupant filter selects occupants whose attribute has the stored resource key.
class OcInitAttribTest : public OcIteratorTest {
public:
  OcInitAttribTest();
  virtual ~OcInitAttribTest();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

private:
  friend class OcInitAttribCache;
  u32 references;
  RsKey key;
};

#endif
