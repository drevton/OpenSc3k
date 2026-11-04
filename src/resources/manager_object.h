#ifndef SC3_RESOURCES_MANAGER_OBJECT_H
#define SC3_RESOURCES_MANAGER_OBJECT_H

#include <deque>
#include <list>
#include <map>

#include "common/sc3_string.h"
#include "common/thread.h"
#include "resources/api.h"
#include "resources/factory.h"
#include "resources/file.h"
#include "resources/indexed_file.h"

class RsDirectoryListing;

// The source list entry stores its owning listing, filename String and directory4/file8 kind, rather than a file size.
struct RsDirectoryEntry {
  RsDirectoryEntry();
  RsDirectoryEntry(RsDirectoryListing *owner, ScString *name, i32 kind);
  RsDirectoryEntry(const RsDirectoryEntry &source);
  // Assign the original owner, concrete filename and entry kind using their ordinary member operations.
  RsDirectoryEntry &operator=(const RsDirectoryEntry &source) = default;
  operator const char *() const;
  bool IsMediaRemovable();
  u32 GetSize();
  u32 GetCreationDate();
  u32 GetLastModificationDate();
  u32 GetSerialNumber();
  void GetFullPath(ScString *out);
  bool CreatePhysicalEntry();
  u32 GetExtension(ScString *out);
  u32 GetStem(ScString *out);
  RsDirectoryListing *owner;
  ScString name;
  i32 kind;
};

int operator==(const RsDirectoryEntry &left, const RsDirectoryEntry &right);
int operator<(const RsDirectoryEntry &left, const RsDirectoryEntry &right);

// Original directory enumeration owns a linked entry list, path, wildcard and the two dot-entry inclusion flags.
class RsDirectoryListing {
public:
  enum class DriveType : i32 { Unknown = 0, Fixed = 1, Removable = 2, Cdrom = 3, Ramdisk = 4, Remote = 5 };
  RsDirectoryListing(const ScString &path, bool refresh, bool includeParent, bool includeCurrent);
  static bool ReadAvailableDrivesIntoStringList(std::list<ScString> *names, std::list<DriveType> *types);
  ScString GetPath();
  bool IsDirectoryRoot();
  operator const char *() const;
  static bool DoesDirectoryExist(const ScString &directory);
  static bool DoesAnyEntryExistThatMatchesPattern(const ScString &pattern);
  static bool RemoveDirectoryA(const ScString &directory);
  static bool CreateDirectoryA(const ScString &directory);
  static void SplitDirectoryPath(const ScString &source, ScString *directory, ScString *leaf);
  bool Refresh();
  u32 Size();
  RsDirectoryEntry *GetEntry(u32 index);
  bool ChangeDirectory(const ScString &directory, bool refresh);
  void SetNewDirectoryEntryFilter(const ScString &filter, bool refresh);
  bool ReadDirectorySpecificEntriesIntoStringList(std::list<ScString> *out, u32 flags);
  bool ReadDirectoryFileEntriesIntoStringList(std::list<ScString> *out);
  bool ReadDirectoryDirectoryEntriesIntoStringList(std::list<ScString> *out, u32 flags);
  bool ReadDirectorySpecificEntriesIntoStringListRecursive(ScString *directory, std::list<ScString> *out, bool first);
  std::list<RsDirectoryEntry> entries;
  ScString path, wildcard;
  u8 includeParent, includeCurrent;

protected:
  bool ContainsEntry(const ScString &name);
  void EnsureSeparator();
};

struct RsCacheKeyLess {
  bool operator()(const RsKey &first, const RsKey &second) const;
};

// Concrete manager constructor11EA4: service/module bases, distinct manager view, registries, cache and locks.
// Original STL storage is represented directly, without foreign layouts or native lifetime/initialized-byte adapters.
class RsManagerObject : public RsServiceBase, public RsManager {
public:
  RsManagerObject();
  ~RsManagerObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnIdle(u32 argument) override;
  bool GetResource(const RsKey &key, u32 iid, void **out) override;
  bool GetPrivateResource(const RsKey &key, u32 iid, void **out) override;
  bool GetNewResource(const RsKey &key, u32 iid, void **out) override;
  bool GetNewResource(u32 type, u32 iid, void **out) override;
  bool RemoveResource(const RsKey &key) override;
  bool Save(const RsKey &key, RsDatabaseSegment *segment) override;
  bool Save(RsKeyListInterface *keys, RsDatabaseSegment *segment) override;
  bool GetResourceList(RsKeyListInterface **out) override;
  bool GetResourceListForType(RsKeyListInterface **out, u32 type) override;
  bool GetAvailableResourceListForType(RsKeyListInterface **out, u32 type) override;
  bool TestForKey(RsKey *key) override;
  bool RegisterObjectFactory(u32 classId, u32 type) override;
  bool UnregisterObjectFactory(u32 classId) override;
  bool FindObjectFactory(u32 type, RsFactoryInterface **out) override;
  bool FindObjectFactory(const RsKey &key, RsFactoryInterface **out) override;
  bool FindObjectFactory(const RsResource &resource, RsFactoryInterface **out) override;
  u32 GetFactoryCount() override;
  RsFactoryInterface *GetFactoryByIndex(u32 index) override;
  bool RegisterDBSegment(RsDatabaseSegment *segment) override;
  bool RegisterDBSegmentFront(RsDatabaseSegment *segment) override;
  bool RegisterDBSegmentBack(RsDatabaseSegment *segment) override;
  bool UnregisterDBSegment(RsDatabaseSegment *segment) override;
  bool TestDBSegment(RsDatabaseSegment *segment) override;
  bool FindDBSegment(const RsKey &key, RsDatabaseSegment **out) override;
  bool FindDBSegment(u32 segmentId, RsDatabaseSegment **out) override;
  u32 GetSegmentCount() override;
  RsDatabaseSegment *GetSegmentByIndex(u32 index) override;
  bool OpenDBRecord(const RsKey &key, RsRecord **out, bool write) override;
  bool CloseDBRecord(const RsKey &key, RsRecord **record) override;
  bool IsGarbageCollectionActive() override;
  void SetGarbageCollectionActive(bool value) override;
  void ForceGarbageCollection() override;

private:
  u8 LoadResource(const RsKey &key, u32 iid, void **out);
  u8 LoadRecord(const RsKey &key, RsFactoryInterface *factory, RsDatabaseSegment *segment, u32 iid, void **out);
  u8 LoadRedirect(
      const RsKey &key, RsFactoryInterface *factory, RsDatabaseSegment *segment, RsStream *stream, u32 iid, void **out);
  u8 CreateResource(const RsKey &key, u32 iid, void **out);
  u8 SaveResource(RsResource *resource, const RsKey &key, RsDatabaseSegment *segment);
  u8 CollectGarbage();
  u32 NextInstance();
  u32 NewResourceGroup();
  ScLock factoryLock;
  std::multimap<u32, RsFactoryInterface *> factories;
  std::multimap<u32, u32> aliases;
  ScLock segmentLock;
  std::deque<RsDatabaseSegment *> segments;
  ScLock cacheLock;
  std::map<RsKey, RsResource *, RsCacheKeyLess> cache;
  ScLock collectionLock;
  u32 nextInstance, newResourceGroup;
  bool collectionActive;
};

// Scope the actual manager lock, retaining its original recursive enter/leave ordering on exceptional exits.
class RsManagerLock {
public:
  // Enter the selected owned recursive lock before a registry or cache operation.
  explicit RsManagerLock(ScLock *lock) : lock(lock) {
    lock->Lock();
  }

  // Leave the same lock after all reached callbacks and output effects.
  ~RsManagerLock() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Actual primary IID7F of registered directory-files classA079CE1B. The segment has a separate controlling identity.
class RsDirectoryAccess : public ScUnknown {
public:
  virtual bool Init(u32 identifier, const ScStringView *name, bool resolved) = 0;
  virtual bool Shutdown() = 0;
  virtual ScStringView *GetDirectory() = 0;
  virtual u32 GetID() = 0;
  virtual RsExtensionMap *GetExtensionMap() = 0;
  virtual u8 FindName(const RsKey &key, ScStringView *out) = 0;
  virtual u8 GetFileName(const RsKey &key, ScStringView *out) = 0;
  virtual u8 MakeKey(const ScStringView *name, const ScStringView *extension, RsKey *out) = 0;
  virtual u8 NewKey(u32 type, RsKey *out) = 0;
  virtual u8 CloneExtensionMap(RsExtensionMap **out) = 0;
  virtual RsDatabaseSegment *GetSegment() = 0;
};

// Original TGI ordering compares unsigned group, instance and type words in that order.
struct RsDirectoryKeyLess {
  bool operator()(const RsKey &left, const RsKey &right) const;
};

// Concrete directory-files producer owns its extension map, cached listing and unique key-to-open-record tree.
class RsDirectorySegment : public RsDirectoryAccess, public RsSegment {
public:
  RsDirectorySegment();
  ~RsDirectorySegment() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 identifier, const ScStringView *name, bool resolved) override;
  bool Shutdown() override;
  ScStringView *GetDirectory() override;
  u32 GetID() override;
  RsExtensionMap *GetExtensionMap() override;
  u8 FindName(const RsKey &key, ScStringView *out) override;
  u8 GetFileName(const RsKey &key, ScStringView *out) override;
  u8 MakeKey(const ScStringView *name, const ScStringView *extension, RsKey *out) override;
  u8 NewKey(u32 type, RsKey *out) override;
  u8 CloneExtensionMap(RsExtensionMap **out) override;
  RsDatabaseSegment *GetSegment() override;
  bool GetResourceKeyList(RsKeyListInterface *out) override;
  bool OpenDBSegment(bool read, bool write) override;
  bool DoOpenRecord(const RsKey &key, RsRecord **record) override;
  bool DoCloseRecord(RsRecord **record) override;
  bool DoAbortRecord(RsRecord **record) override;
  bool DoTestForRecord(const RsKey &key) override;
  bool DoOpenRecordForWrite(const RsKey &key, RsRecord **record) override;
  bool DoCreateNewRecord(const RsKey &key, RsRecord **record) override;
  bool DoDeleteRecord(const RsKey &key) override;
  u8 KeyName(const RsKey &key, ScString *instance, ScStringView *extension);
  u8 OpenFileRecord(const RsKey &key, RsRecord **record, u32 mode);
  u8 RefreshDirectory();
  ScString path;
  RsDirectoryListing *listing;
  u32 identifier;
  RsExtensionMap extensions;
  u8 ready;
  u32 nextInstance;
  std::map<RsKey, RsLooseRecord *, RsDirectoryKeyLess> records;
};

// Original registry callback parses one directory-segment extension association row.
void RsDirectoryRegistryCallback(const ScStringView &key, const ScStringView &value, void *context);

#endif
