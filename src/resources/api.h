#ifndef SC3_RESOURCES_API_H
#define SC3_RESOURCES_API_H

#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "resources/factory.h"
#include "resources/resource.h"

class RsFileSystem;
class ScLanguageManager;

RsFileSystem *RsGetFileSystem();
ScLanguageManager *RsGetLanguageManager();

// Copyright (C) 2025 Nelson Gomez
// Compression interface declarations adapted from Scion; LGPL-2.1-or-later.

class ScStringView;

// Original IID8359908E compression interface, shared by the resource DLL's QFS and LZO codec identities.
// Mutable reference outputs use explicit pointers while retaining the original parameter order and pointee types.
class RsCompression : public ScUnknown {
public:
  virtual bool Compress(const void *source, u32 size, u8 *destination, u32 *written) = 0;
  virtual bool Decompress(const u8 *source, u32 size, void *destination, u32 *written) = 0;
  virtual u32 GetBufferSize(u32 size) = 0;
  virtual u32 GetCompressedSize(const u8 *source) = 0;
  virtual u32 GetUncompressedSize(const u8 *source) = 0;
  virtual void GetVersionString(ScStringView *out) = 0;
};

// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.
/*
 * sc3k-gzcom-dll - an open-source DLL Plugin SDK for SimCity 3000
 *
 * cIGZDBSegment.h
 *
 * Copyright (C) 2025 Nicholas Hayes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, under
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

// Complete SDK database-segment method contract, with mutable/output references spelled as explicit pointers.
// Implementations preserve original record ownership; native pointers do not describe a foreign x86 layout.
class ScStringView;
class RsKeyListInterface;

class RsDatabaseSegment : public ScUnknown {
public:
  virtual bool Init(u32 identifier, const ScStringView *name, bool resolved) = 0; // Original +12.
  virtual bool Shutdown() = 0;                                                    // Original +16.
  virtual u32 GetID() = 0;                                                        // Original +20.
  virtual bool GetResourceKeyList(RsKeyListInterface *out) = 0;                   // Original +24.
  virtual bool OpenDBSegment(bool read, bool write) = 0;                          // Original +28.
  virtual bool OpenRecord(const RsKey &key, RsRecord **record) = 0;               // Original +32.
  virtual bool CloseRecord(RsRecord **record) = 0;                                // Original +36.
  virtual bool AbortRecord(RsRecord **record) = 0;                                // Original +40.
  virtual bool TestForRecord(const RsKey &key) = 0;                               // Original +44.
  virtual bool OpenRecordForWrite(const RsKey &key, RsRecord **record) = 0;       // Original +48.
  virtual bool CreateNewRecord(const RsKey &key, RsRecord **record) = 0;          // Original +52.
  virtual bool DeleteRecord(const RsKey &key) = 0;                                // Original +56.
};

// Actual no-argument class constructors registered by the resource module. Each registered producer has its actual concrete implementation.
ScUnknown *RsCreateClass199925();
ScUnknown *RsCreateArchiveSegment();
ScUnknown *RsCreateKeyList();
ScUnknown *RsCreateClassA079CE1B();
ScUnknown *RsCreateTextFactory();
ScUnknown *RsCreateClass835F62CF();
ScUnknown *RsCreateManager();
ScUnknown *RsCreateClass35990A3();
ScUnknown *RsCreateLzoCodec();
ScUnknown *RsCreateClass2AE197F();
ScUnknown *RsCreateClass5E3();
ScUnknown *RsCreateClass42E411C2();
ScUnknown *RsCreateClass1A1E8FFC();
ScUnknown *RsCreateImageFactory();

// Copyright (C) 2025 Nelson Gomez
// File interface declarations adapted from Scion with verified Unlimited corrections; LGPL-2.1-or-later.

class ScStringView;

// Local file error words preserve the open/save paths' existing native-error mappings.
enum RsFileErrorCode : u32 {
  kRsFileErrorNone = 0x20000000,
  kRsFileErrorAlreadyExists = 0x20000001,
  kRsFileErrorAccessFailure = 0x20000002,
  kRsFileErrorNameTooLong = 0x20000006,
  kRsFileErrorInvalidDirectory = 0x20000007,
};

// File access bits map separately to native read/write flags.
enum RsFileAccess : i32 {
  kRsFileAccessRead = 1,
  kRsFileAccessWrite = 2,
  kRsFileAccessReadWrite = 3,
};

// Creation modes preserve the original five dispositions and their existing-file behavior.
enum RsFileCreation : i32 {
  kRsFileCreateNew = 0,
  kRsFileCreateAlways = 1,
  kRsFileOpenExisting = 2,
  kRsFileOpenAlways = 3,
  kRsFileTruncateExisting = 4,
};

// File sharing bits independently permit other readers and writers.
enum RsFileSharing : i32 {
  kRsFileShareNone = 0,
  kRsFileShareRead = 1,
  kRsFileShareWrite = 2,
  kRsFileShareReadWrite = 3,
};

// File positions returned after a failed seek are outside the supported position domain.
inline constexpr u32 kRsInvalidFilePosition = UINT32_MAX;

// General seek origins select the beginning, current cursor or end of a file.
enum RsSeekOrigin : i32 {
  kRsSeekBegin = 0,
  kRsSeekCurrent = 1,
  kRsSeekEnd = 2,
};

// Installed IID E0203645 file contract, ending at the two path setters. The later reference buffering methods
// do not occur in this Unlimited interface. Overload declaration order preserves the actual MSVC virtual groups.
class RsFileInterface : public ScUnknown {
public:
  virtual bool Open(i32 access, i32 creation, i32 share) = 0;
  virtual bool IsOpen() const = 0;
  virtual bool Close() = 0;
  virtual u32 Position() = 0;
  virtual u32 Size() = 0;
  virtual bool Resize(u32 size) = 0;
  virtual u32 SeekBegin() = 0;
  virtual u32 SeekEnd() = 0;
  virtual u32 SeekRelative(i32 distance) = 0;
  virtual u32 SeekAbsolute(u32 offset) = 0;
  virtual u32 Seek(i32 distance, i32 origin) = 0;
  virtual bool Read(void *out, u32 *size) = 0;
  virtual bool Read(void *out, u32 size) = 0;
  virtual bool Write(const void *bytes, u32 *size) = 0;
  virtual bool Write(const void *bytes, u32 size) = 0;
  virtual bool Flush() = 0;
  virtual bool Delete() = 0;
  virtual bool Rename(const ScStringView &name) = 0;
  virtual bool Exists() = 0;
  virtual bool Copy(const ScStringView &source, bool overwrite) = 0;
  virtual bool Copy(const RsFileInterface &source, bool overwrite) = 0;
  virtual void GetName(ScStringView *out) const = 0;
  virtual const ScStringView *GetName() const = 0;
  virtual const char *GetNameData() const = 0;
  virtual void SetName(const ScStringView &name) = 0;
  virtual void SetName(const char *name) = 0;
};

struct RsKey;
class ScStringView;

// Actual optional database-segment IIDA329F979, with original operations through secondary slot48.
class RsFileSegment : public ScUnknown {
public:
  // Initialize the file-backed segment through its original secondary slot12; concrete producers supply the body.
  virtual bool Init(u32, const ScStringView *, bool) = 0;

  // Shut down the file-backed segment through secondary slot16; incomplete native implementers remain explicit.
  virtual bool Shutdown() = 0;

  // Copy the segment path through the caller's mutable String interface at original secondary slot20.
  virtual u8 GetPath(ScStringView *) = 0;

  // Select a resolved read path through original secondary slot24.
  virtual u8 OpenRead(const ScStringView *) = 0;

  // Select a resolved write path through original secondary slot28.
  virtual u8 OpenWrite(const ScStringView *) = 0;

  // Query the attached archive's stored format version through original secondary slot32.
  virtual u32 GetVersion() = 0;

  // Load the stored layer directory through secondary slot36; concrete producers supply the transport.
  virtual u8 LoadRecords() = 0;

  // Store active records through secondary slot40 (GZResourceD1000F5C4, RET12); the producer publishes the key on success.
  virtual u8 StoreRecords(RsKey *, u32, u32) = 0;

  // Select an existing stored layer by its serialized key through original secondary slot44.
  virtual u8 SelectLayer(const RsKey &) = 0;

  // Clear the active stored layer through original secondary slot48.
  virtual u8 ClearSelection() = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Recovered cIGZFileSystem prefix, matching the SDK and the installed service2073215D/IIDE0203660.
// Retail cGZFileSystem symbols prove borrowed const character results; the newer SDK mutable spelling is incorrect.
class RsFileSystem : public ScUnknown {
public:
  static constexpr u32 ServiceId = kRsFileSystemServiceId, Iid = IID_RsFileSystem;
  virtual const char *AppDrive() = 0;
  virtual const char *AppDirectory() = 0;
  virtual const char *AppName() = 0;
  virtual const char *AppExtension() = 0;
  virtual const char *DataDirectory() = 0;
  virtual const char *PlugInDirectory() = 0;

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual const char *CDAppDrive() = 0;
  virtual const char *CDAppDirectory() = 0;
  virtual const char *CDDataDirectory() = 0;
  virtual const char *CDPlugInDirectory() = 0;
  virtual bool FindCDDrive() = 0;
  virtual void UseCDIniFile(bool) = 0;
  virtual void SetExpectedCDName(ScStringView const &) = 0;
  virtual void SetExpectedCDAppPath(ScStringView const &) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Complete SDK manager contract in its verified Unlimited virtual order, including MSVC overload groups.
class RsManager : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual bool GetResource(const RsKey &key, u32 iid, void **out) = 0;
  virtual bool GetPrivateResource(const RsKey &key, u32 iid, void **out) = 0;
  virtual bool GetNewResource(const RsKey &key, u32 iid, void **out) = 0;
  virtual bool GetNewResource(u32 type, u32 iid, void **out) = 0;
  virtual bool RemoveResource(const RsKey &key) = 0;
  virtual bool Save(const RsKey &key, RsDatabaseSegment *segment) = 0;
  virtual bool Save(RsKeyListInterface *keys, RsDatabaseSegment *segment) = 0;
  virtual bool TestForKey(RsKey *key) = 0;
  virtual bool GetResourceList(RsKeyListInterface **out) = 0;
  virtual bool GetResourceListForType(RsKeyListInterface **out, u32 type) = 0;
  virtual bool GetAvailableResourceListForType(RsKeyListInterface **out, u32 type) = 0;
  virtual bool RegisterObjectFactory(u32 classId, u32 type) = 0;
  virtual bool UnregisterObjectFactory(u32 classId) = 0;
  virtual bool FindObjectFactory(const RsResource &resource, RsFactoryInterface **out) = 0;
  virtual bool FindObjectFactory(const RsKey &key, RsFactoryInterface **out) = 0;
  virtual bool FindObjectFactory(u32 type, RsFactoryInterface **out) = 0;
  virtual u32 GetFactoryCount() = 0;
  virtual RsFactoryInterface *GetFactoryByIndex(u32 index) = 0;
  virtual bool RegisterDBSegment(RsDatabaseSegment *segment) = 0;
  virtual bool RegisterDBSegmentFront(RsDatabaseSegment *segment) = 0;
  virtual bool RegisterDBSegmentBack(RsDatabaseSegment *segment) = 0;
  virtual bool UnregisterDBSegment(RsDatabaseSegment *segment) = 0;
  virtual bool TestDBSegment(RsDatabaseSegment *segment) = 0;
  virtual bool FindDBSegment(const RsKey &key, RsDatabaseSegment **out) = 0;
  virtual bool FindDBSegment(u32 segmentId, RsDatabaseSegment **out) = 0;
  virtual u32 GetSegmentCount() = 0;
  virtual RsDatabaseSegment *GetSegmentByIndex(u32 index) = 0;
  virtual bool OpenDBRecord(const RsKey &key, RsRecord **out, bool write) = 0;
  virtual bool CloseDBRecord(const RsKey &key, RsRecord **record) = 0;
  virtual bool IsGarbageCollectionActive() = 0;
  virtual void SetGarbageCollectionActive(bool active) = 0;
  virtual void ForceGarbageCollection() = 0;
};

// Installed image-factory options IID A2C72982, preserving the complete operation order from table1001CBF8.
// Private transport and setter nominal return declarations retain their previously documented limits.
class ScString;
class GxBuffer;
struct HINSTANCE__;

class RsImageOptions : public ScUnknown {
public:
  virtual bool LoadFile(ScString *name, GxBuffer **out, u8 compressed, const GxColorType *format, u32 unused) = 0;
  virtual u8 SaveFile(const ScStringView *name, GxBuffer *source) = 0;
  virtual u8 LoadModuleBitmap(HINSTANCE__ *module, u16 identifier, GxBuffer **out) = 0;
  virtual u8 LoadExecutableBitmap(u16 identifier, GxBuffer **out, u8 compressed, const GxColorType *format, u32 unused) = 0;
  virtual u8 LoadStream(GxBuffer **out, RsStream *stream, u32 unused) = 0;
  virtual u8 SaveStream(GxBuffer *source, RsStream *stream) = 0;
  virtual void SetMode(GxVramRequest mode) = 0;
  virtual void SetFormat(const GxColorType &format) = 0;
  virtual bool PushEncoding(u32 encoding) = 0;
  virtual bool PopEncoding() = 0;
  virtual u32 GetEncoding() = 0;
};

u32 RsQfsEncodeBody(const u8 *source, i32 length, u8 *destination, i32 window, i32 fast);
u32 SC3_STDCALL RsQfsEncodeBodyAlt(const u8 *source, i32 length, u8 *destination, i32 window, i32 fast);
u32 RsQfsEncode(const u8 *source, i32 length, u8 *destination, const i32 *fast);
u32 SC3_STDCALL RsQfsEncodeAlt(const u8 *source, i32 length, u8 *destination, const i32 *unused);

// Original eight-byte property descriptor: the key, element type and element count are compared independently.
struct RsProperty {
  u32 id;
  u16 type, count;
};

static_assert(sizeof(RsProperty) == 8);

class ScStringView;
class RsStream;

// Original property-record operations use authenticated Boolean returns and the installed Float32/Sint32/Sint64 order.
// Constant inputs retain original references; mutable outputs use typed pointers under the project source rule.
class RsPropertySet : public ScUnknown {
public:
  virtual bool Read(RsStream *stream) = 0;                                                        // Original +12.
  virtual bool ReadWithFormat(RsStream *stream, u32 format) = 0;                                  // Original +16.
  virtual bool InitText(RsStream *stream) = 0;                                                    // Original +20.
  virtual bool InitBinary(RsStream *stream) = 0;                                                  // Original +24.
  virtual bool CopyKey(RsKey *out) const = 0;                                                     // Original +28.
  virtual u32 GetFormat() const = 0;                                                              // Original +32.
  virtual bool IsText() const = 0;                                                                // Original +36.
  virtual bool HasProperty(const RsProperty &property) = 0;                                       // Original +40.
  virtual bool SetName(const RsProperty &property, ScStringView *name) = 0;                       // Original +44.
  virtual bool GetByte(const RsProperty &property, u8 *out, u32 index) = 0;                   // Original +48.
  virtual bool GetU16(const RsProperty &property, u16 *out, u32 index) = 0;                       // Original +52.
  virtual bool GetWord(const RsProperty &property, u32 *out, u32 index) = 0;                  // Original +56.
  virtual bool GetF32(const RsProperty &property, f32 *out, u32 index) = 0;                       // Original +60.
  virtual bool GetI32(const RsProperty &property, i32 *out, u32 index) = 0;                       // Original +64.
  virtual bool GetI64(const RsProperty &property, i64 *out, u32 index) = 0;                       // Original +68.
  virtual bool GetText(const RsProperty &property, ScStringView *out, u32 index) = 0;             // Original +72.
  virtual bool GetKey(const RsProperty &property, RsKey *out, u32 index) = 0;                 // Original +76.
  virtual bool SetByte(const RsProperty &property, const u8 &value, u32 index) = 0;           // Original +80.
  virtual bool SetU16(const RsProperty &property, const u16 &value, u32 index) = 0;               // Original +84.
  virtual bool SetWord(const RsProperty &property, const u32 &value, u32 index) = 0;          // Original +88.
  virtual bool SetF32(const RsProperty &property, const f32 &value, u32 index) = 0;               // Original +92.
  virtual bool SetI32(const RsProperty &property, const i32 &value, u32 index) = 0;               // Original +96.
  virtual bool SetI64(const RsProperty &property, const i64 &value, u32 index) = 0;               // Original +100.
  virtual bool SetText(const RsProperty &property, const ScStringView &value, u32 index) = 0; // Original +104.
  virtual bool SetKey(const RsProperty &property, const RsKey &value, u32 index) = 0;         // Original +108.
};

#endif
