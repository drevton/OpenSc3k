#ifndef SC3_NETWORK_API_H
#define SC3_NETWORK_API_H

#include <stddef.h>

#include <list>
#include <string>
#include <vector>

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "occupants/animation_manager.h"

// A placement code stores its piece WORD above the state BYTE. Image-instance keys use their separate packing.
inline constexpr u32 kNwPieceCodeShift = 8;
inline constexpr u32 kNwPieceImageInstanceShift = 8;

// Construction and neighbor-connection selectors, distinct from city taxonomy and packed piece IDs.
enum NwNetworkKind : i32 {
  kNwRoadNetwork = 1,
  kNwRailNetwork = 2,
  kNwPowerNetwork = 3,
  kNwHighwayNetwork = 4,
  kNwPipeNetwork = 5,
  kNwSubwayNetwork = 6,
};

// SDK attribute declarations: Copyright (C) 2026 Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the SDK source and retained Unlimited corrections.
class ScStringView;
class CtAgentType;
class SpDefinition;
struct RsKey;
class ScOccupant;
class NwTrafficBlock;

// IID41658D28 primary virtual order, verified against installed table2B788 and retained Linux method symbols.
// The private interface is absent from the SDK. Value-storing setter returns below remain qualified effect projections.
class NwConnection : public ScUnknown {
public:
  virtual ScOccupant *AsOccupant() = 0;           // Original12, borrowed secondary occupant view.
  virtual bool Init(u32 piece, u32 rotation) = 0; // Original16, two full unsigned DWORD inputs.
  // Original20 defines no MSVC return channel; the original repair operation returns void.
  virtual void Repair() = 0;
  virtual u32 NeedsRepair() const = 0; // Original24 defines EAX as 0/1; nominal byte/DWORD return remains unresolved.
  // Original28 leaves a packed word in EAX whose low byte need not be Boolean. Void/value return remains unresolved.
  virtual void SetDamage(u8 value) = 0;
  virtual u8 ConnectionFlag0() const = 0; // Original32, preserves raw AL from the attribute interface.
  // Original36 returns void: its body is RET4 with no result channel. Windows bool/BYTE input spelling is unresolved.
  virtual void SetIsDiagonal(u8 value) = 0;
  virtual u8 ConnectionFlag1() const = 0; // Original40, preserves raw AL from the attribute interface.
  // Original44 returns void: its body is RET4 with no result channel. Windows bool/BYTE input spelling is unresolved.
  virtual void SetCollapse(u8 value) = 0;
  // The unsigned native carrier preserves zero extension; the original signedness/type alias remains unresolved.
  virtual u32 TileId() const = 0;
  // Original52 takes a DWORD and leaves the old packed word in EAX. Boolean return is excluded; void/value is unresolved.
  virtual void SetTileId(u32 piece) = 0;
  virtual u32 GetRotationFlags() = 0; // Original56 defines EAX as 0..3; nominal byte/DWORD return remains unresolved.
  // Original60 takes a DWORD and leaves the new packed location in EAX. Boolean is excluded; void/value is unresolved.
  virtual void SetRotationFlags(u32 rotation) = 0;
  // Original64 defines EAX as 0/1; AL consumers do not prove its nominal return width.
  virtual int IsTeleport() const = 0;
  // Original68 leaves the new packed word in EAX. Boolean return is excluded; the void/value return is unresolved.
  virtual void SetTeleport(i32 direction, i32 length) = 0;
  // Original72 defines EAX as 0/1 and two DWORD outputs; sampled callers discard EAX, so nominal return width is unresolved.
  virtual int GetTeleport(i32 *direction, i32 *length) = 0;
  // Original76 leaves QI/Release register residue, which can be non-Boolean. Original void/value return is unresolved.
  virtual void GetConnectionSamples(
      i32 kind, u8 plane, i32 side, u8 *first, u8 *second, u8 *third, u8 *fourth) const = 0;
  virtual u8 GetTrafficKey(RsKey *out) = 0;         // Original80 defines only AL; bool/BYTE spelling unresolved.
  virtual NwTrafficBlock *GetTrafficBlock() = 0;    // Original84, borrowed cached block or null.
  virtual u8 ClearTrafficBlock() = 0;               // Original88 defines only AL; bool/BYTE spelling unresolved.
  virtual u32 GetSubTypeData(u32 mask) = 0;         // Original92, actual piece-specific flag mask.
  virtual RsKey AsResourceKey(u32 piece) const = 0; // Original96, aggregate return with a hidden PC buffer.
};

// IID82237425 has the complete occupant-attribute prefix at12..172 before its connection operations.
// The private interface's historical base hierarchy is unresolved; matching slots do not prove GeAttrib inheritance.
// Retain Unlimited byte classification results and provisional SDK producer carriers. MSVC reverses the sprite overloads.
class NwConnectionData : public ScUnknown {
public:
  virtual bool GetOccupantGZCLSID(u32 *) const = 0;
  virtual bool SetOccupantGZCLSID(u32) = 0;
  virtual RsKey *Key() const = 0;
  virtual bool IsGeomFlag(u32) = 0;
  virtual u8 SetGeomFlag(u32 mask) = 0;
  virtual u8 ClearGeomFlag(u32 mask) = 0;
  virtual u32 GetGeomFlag() = 0;
  virtual u8 GetOccLayerFlag() = 0;
  virtual void SetOccLayerFlag(u8 mask) = 0;
  virtual u8 MajorType() const = 0;
  virtual u8 SubTypeInfo() const = 0;
  virtual u8 AnimType() const = 0;
  virtual bool GetName(ScStringView *out) = 0;
  virtual u32 SizeInCellsX() = 0;
  virtual u32 SizeInCellsZ() = 0;
  virtual u32 SizeInCellsY() = 0;
  virtual CtAgentType *CityAgentType() = 0;
  virtual u32 TypeId() const = 0;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) = 0;
  virtual SpDefinition *GetSpriteAttrib() = 0;
  virtual SpDefinition *GetOriginalSpriteAttrib() = 0;
  virtual u32 GetDefaultLayerFlag() const = 0;
  virtual bool SetDefaultLayerFlag(u32) = 0;
  virtual iptr GetPeopleProducerAttrib() = 0;
  virtual iptr GetVehicleProducerAttrib() = 0;
  virtual u8 Flammability() = 0;
  virtual u32 GetSoundObjectId(i32 *index) = 0;
  virtual void GetSoundKeyA(RsKey *) = 0;
  virtual void GetSoundKeyB(RsKey *) = 0;
  virtual void GetPlopCursorKey(RsKey *) = 0;
  virtual void GetQueryDescKey(RsKey *) = 0;
  virtual void GetQueryFormatFlags(u32 *) = 0;
  virtual u32 GetQueryDialogClass() = 0;
  virtual bool SetQueryDialogClass(u32) = 0;
  virtual bool DebugInstanceTag(ScStringView *) = 0;
  virtual bool DebugClassTag(ScStringView *) = 0;
  virtual bool DebugTypeTag(ScStringView *) = 0;
  virtual bool GetNameKey(RsKey *) = 0;
  virtual void SetOverRideAttrib(OcOverride *) = 0;
  virtual OcOverride *GetOverRideAttrib() = 0;
  virtual bool GetArchitectName(ScStringView *) = 0;
  // Original +176 effects; the private nominal return declaration remains unresolved.
  virtual void GetConnectionSamples(
      i32 kind, u8 plane, i32 side, i32 rotation, u8 *first, u8 *second, u8 *third, u8 *fourth) const = 0;
  // Original +180 effects; the unused private nominal return declaration remains unresolved.
  virtual void SetPrimaryMask(u32 value) = 0;
  virtual u8 ConnectionFlag0() const = 0; // Original +184; AL producer.
  // Original +188 effects; the unused private nominal return declaration remains unresolved.
  virtual void SetConnectionFlag0(u8 value) = 0;
  virtual u8 ConnectionFlag1() const = 0; // Original +192; AL producer.
  // Original +196 effects; the unused private nominal return declaration remains unresolved.
  virtual void SetConnectionFlag1(u8 value) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3Internet.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

class NwInternet;
class NwChatSystem;

// Installed SC3U cISC3WinGraphicsViewer vtable; this interface has no ScUnknown prefix.
class NwGraphicsViewer {
public:
  virtual bool CreateGraphicsViewerWindow(WnWindow **window) = 0;
  virtual bool DestroyGraphicsViewerWindow() = 0;
  virtual bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) = 0;
  virtual WnWindow *GetIGZWin() = 0;
  virtual void GetViewerBaseDirectoryPath(ScStringView *out) = 0;
  virtual bool SetViewerBaseDirectoryPath(const ScStringView &path) = 0;
  virtual void GetViewerTransferDirectoryName(ScStringView *out) = 0;
  virtual void GetViewerTransferDirectoryPath(ScStringView *out) = 0;
  virtual void GetCurrentViewerDirectoryName(ScStringView *out) = 0;
  virtual bool SetCurrentViewerDirectoryName(const ScStringView &name) = 0;
  virtual bool GoToAlbumForCurrentCity(bool create) = 0;
  virtual bool GoToAlbum(u32 index) = 0;
  virtual bool GoToAlbum(const ScString &name) = 0;
  virtual bool GoToAlbum(const ScString &name, u32 index, bool create) = 0;
  virtual bool GoToTransferAlbum() = 0;
  virtual bool AlbumExists(const ScString &name, u32 index) = 0;
  virtual bool CreateAlbum(const ScString &name, u32 index) = 0;
  virtual bool DeleteAlbum(u32 index) = 0;
  virtual bool DeleteAlbum(const ScString &name) = 0;
  virtual bool AddNewGraphic(const ScString &path, const ScString &information, const ScString &description,
      const ScString &city, u32 identifier, bool flag) = 0;
  virtual bool AddNewTransferGraphic(const ScString &path, bool flag) = 0;
  virtual bool GoToGraphic(const ScString &name) = 0;
  virtual bool GoToNextGraphic() = 0;
  virtual bool GoToPreviousGraphic() = 0;
  virtual bool DeleteCurrentGraphic() = 0;
  virtual bool RefreshGraphicList(bool *changed) = 0;
  virtual bool MakeSureCurrentAlbumDirectoryExists(bool reset) = 0;
  virtual void SetIdleProcessingInterval(u32 interval) = 0;
  virtual void DoIdleProcessing() = 0;
  virtual bool SetCurrentFile(const ScString &name, bool flag) = 0;
  virtual bool SetInitialFile() = 0;
  virtual bool GetNewTempFileNameForCurrentAlbum(ScString *out) = 0;
  virtual bool GetNewTempFileName(const ScString &directory, ScString *out) = 0;
  virtual bool GetInformationOfFile(const ScString &path, ScString *out) = 0;
  virtual bool GetInformationOfTransferFile(const ScString &path, ScString *out) = 0;
  virtual bool GetInformationOfStandardFile(const ScString &path, ScString *out) = 0;
  virtual bool SaveInformationOfStandardFile(const ScString &path, const ScString &information) = 0;
  virtual bool GetDescriptionOfFile(const ScString &path, ScString *out) = 0;
  virtual bool SaveDescriptionOfFile(const ScString &path, const ScString &description) = 0;
  virtual bool SaveDescriptionOfFile() = 0;
};
enum class NwInternetCommand : i32;

// These parameters are the original vector<cRZString>, not a replacement collection protocol.
using NwChatStrings = std::vector<ScString>;

// Complete cISC3TransferSystem contract from retail CodeView2582 and the matching concrete public methods.
// TransferType values and the concrete transfer engine remain unreconstructed rather than being guessed here.
class NwTransferSystem : public ScUnknown {
public:
  enum class TransferType : i32;
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual void SetSC3Internet(NwInternet *internet) = 0;
  virtual void SetParentWindow(WnWindow *window) = 0;
  virtual bool ShowImageBrowser(bool first, bool second, ScStringView *name, bool fourth) = 0;
  virtual bool IsImageBrowserShown() = 0;
  virtual NwGraphicsViewer *GetImageBrowser() = 0;
  virtual bool TakeSnapshot(u32 kind) = 0;
  virtual bool DoUserStringCommand(
      const ScStringView &command, NwInternetCommand type, const NwChatStrings &arguments) = 0;
  virtual bool GetDownloadDirectory(ScStringView *out) = 0;
  virtual bool SetDownloadDirectory(const ScStringView &directory, bool create) = 0;
  virtual bool CopyFileIntoViewerDirectory(const ScStringView &file) = 0;
  virtual bool CopyFileFromViewerDirectoryToAnotherDirectory(
      const ScStringView &file, const ScStringView &directory) = 0;
  virtual bool MakeSureDownloadDirectoryExists(bool create) = 0;
  virtual bool ProcessSendRecvRequest(bool send, TransferType type, const ScStringView &first,
      const ScStringView &second, u32 fifth, u32 sixth, u32 seventh) = 0;
  virtual void DoIdleProcessing() = 0;
  virtual u32 GetGraphicsQuality0to100() = 0;
  virtual void SetGraphicsQuality0to100(u32 quality) = 0;
  virtual void AbortAllTransfers() = 0;
  virtual GxBuffer *GetScreen(TransferType type, bool capture) = 0;
  virtual bool GetNameOfScreen(TransferType type, ScStringView *out) = 0;
};

// The preferences block is a concrete nonvirtual3212-byte value, returned directly by SC3U414A54.
// Descriptive field offsets retain unknown flag/text meanings; the constructor follows installed SC3U004133A4.
struct NwWindowPosition {
  i32 left, top;
};

class NwInternetPrefs {
public:
  NwInternetPrefs();
  u32 field0, field4, field8, field12;
  char text16[384], text400[384], text784[384], text1168[384], text1552[384];
  NwWindowPosition windowPositions[15]; // Original1936; each signed left/top pair starts at INTMAX/INTMAX.
  u32 field2056;
  char text2060[384], text2444[384], text2828[384];
};

// Initialize the internet preferences defaults, empty string prefixes and unset window positions.
inline NwInternetPrefs::NwInternetPrefs() : field0(2), field4(2), field8(1), field12(60), field2056(0) {
  text16[0] = 0;
  text400[0] = 0;
  text784[0] = 0;
  text1168[0] = 0;
  text1552[0] = 0;
  text2060[0] = 0;
  text2444[0] = 0;
  text2828[0] = 0;
  for (NwWindowPosition &position : windowPositions) {
    position.left = INT32_MAX;
    position.top = INT32_MAX;
  }
}

static_assert(sizeof(NwWindowPosition) == 8);
static_assert(sizeof(NwInternetPrefs) == 3212);
static_assert(offsetof(NwInternetPrefs, windowPositions) == 1936);

// Original cISC3Internet; its concrete cSC3Internet is named NwInternetObject in IDA and remains unported.
class NwInternet : public ScUnknown {

public:
  enum class ConnectionState : i32;
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;

  virtual ConnectionState GetConnectionState() = 0;
  virtual bool IsConnected() = 0;
  virtual bool ConnectionIsDialup() = 0;

  virtual bool Connect() = 0;
  virtual bool Disconnect() = 0;

  virtual bool ReadPreferences() = 0;
  virtual bool WritePreferences() = 0;

  virtual void DoIdleProcessing() = 0;

  virtual bool GetMainServerURL(ScStringView *) = 0;
  virtual bool SetMainServerURL(ScStringView const &) = 0;

  virtual void SetParentWindow(WnWindow *) = 0;
  virtual NwChatSystem *GetChatSystem() = 0;
  virtual NwTransferSystem *GetTransferSystem() = 0;

  virtual NwInternetPrefs *GetInternetPrefs() = 0;
  virtual bool GetInternetPrefsFilePath(ScStringView *) = 0;
  virtual bool SetInternetPrefs(NwInternetPrefs *) = 0;

  virtual bool DoUserStringCommand(ScStringView const &) = 0;

  virtual bool ViewInternalBrowser(ScStringView const &, ScString const *, bool) = 0;
  virtual bool ViewExternalBrowser(ScStringView const &) = 0;
  virtual bool IsExternalBrowserAvailable() = 0;

  virtual bool GetCityExchangeUserInfo(ScStringView *, ScStringView *, ScStringView *) = 0;
  virtual bool GetCityExchangeURL(ScStringView *) = 0;

  virtual bool RegisterURLMessage(ScStringView const &, ScStringView const &, bool) = 0;
  virtual bool UnregisterURLMessage(ScStringView const &) = 0;
};

// Genuine COM publication slots are passed directly as void** so callbacks observe the original live global outputs.
// Only the external output storage stays opaque; use the proven interface types at each actual method call.
extern void *NwTrafficFactoryCache;
extern void *NwTrafficPredicate;

// Actual eight-byte source filter receiver, with an unsigned reference count and a virtual destructor after Test.
class NwPredicate : public OcIteratorTest {
public:
  NwPredicate();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;
  virtual ~NwPredicate();
  u32 references;
};

class NwRoadPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRailPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwHighwayPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwSubwayPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPipePredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPowerPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwTransitPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwFloraPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotRoadPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotRailPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotHighwayPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotSubwayPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotPipePredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotPowerPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNotNetworkPredicate : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

// Distinct original predicate vtables whose logical tests share some compiler-folded implementations.
class NwNetworkPredicate : public NwPredicate {};

extern NwPredicate *NwPredicates[42]; // Original globals 100323C0..10032464.

class NwRoadOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPowerOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRailOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwHighwayOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwSubwayOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPipeOverlap : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRoadJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPowerJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRailJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwHighwayJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwSubwayJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPipeJoin : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationA : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationB : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationC : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationD : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationE : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationF : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationG : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationH : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationI : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationJ : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationK : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRotationL : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwDecorEligible : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwDecorated : public NwPredicate {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwTrafficBlock;
class NwVehicle;
struct NwVehicleGraphEdge;

// Original cISSVehiclePath primary interface, including the four operations following force-entry.
// Private byte/Boolean and discarded return spellings remain unresolved; effect-only declarations preserve used inputs.
class NwVehiclePath : public ScUnknown {
public:
  virtual NwVehiclePath *GetCopy() = 0;
  virtual void Rewind() = 0;
  virtual void GetStartPoint(i32 *x, i32 *altitude, i32 *z) = 0;
  virtual void GetEndPoint(i32 *x, i32 *altitude, i32 *z) = 0;
  virtual u8 GetEndOrientation() = 0;
  virtual u8 GetPlacingOrientation() = 0;
  virtual f32 GetPitch() = 0;
  virtual u8 GetDistToEndFromCurrentPoint(i32 *distance) = 0;
  virtual u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) = 0;
  virtual i32 Length() = 0;
  virtual void GetEndCell(u32 *x, u32 *z) = 0;
  virtual void SetSpeed(f32 speed) = 0;
  virtual f32 Speed() = 0;
  virtual NwVehicleGraphEdge *GetEdge() = 0;
  virtual NwVehicle *LastVehicle() = 0;
  virtual u8 EnterVehicle(NwVehicle *vehicle, i32 distance) = 0;
  virtual void VehicleLeftPath(NwVehicle *vehicle) = 0;
  virtual NwTrafficBlock *GetStrtSimTrBlock() = 0;
  virtual u8 ForceEnterVehicle(NwVehicle *vehicle) = 0;
  // Original88/92 preserve the observed block effects; their nominal return declarations remain unresolved.
  virtual void BlockPath() = 0;
  virtual void UnblockPath() = 0;
  virtual i32
  GetReferenceMicroY() = 0;       // Original96 returns the stored 32-bit altitude; source signedness is unresolved.
  virtual u8 SortId(i32 *id) = 0; // Original100 writes the ID; nominal bool/BYTE spelling remains unresolved.
};

#endif
