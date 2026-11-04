#ifndef SC3_OCCUPANTS_API_H
#define SC3_OCCUPANTS_API_H

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Occupant geometry bits identify automata and individual infrastructure memberships.
// Network and vehicle masks require all constituent bits; road/highway selection accepts either infrastructure bit.
enum OcGeometryFlag : u32 {
  kOcGeomAutomata = 0x200,
  kOcGeomFlora = 0x400,
  kOcGeomNetwork = 0x800,
  kOcGeomVehicle = 0x1000,
  kOcGeomPerson = 0x2000,
  // Road, highway, rail and subway pieces carry this bit; utility-only pieces do not.
  kOcGeomTransitPiece = 0x4000,
  kOcGeomRoad = 0x8000,
  kOcGeomHighway = 0x10000,
  kOcGeomRail = 0x20000,
  kOcGeomSubway = 0x40000,
  // Each named road, rail and highway tunnel adds this bit to its ordinary network geometry word.
  kOcGeomTunnel = 0x100000,
  kOcGeomHighwayOnRamp = 0x200000,
  kOcGeomPower = 0x400000,
  kOcGeomPipe = 0x800000,
  kOcGeomTransitNetwork = kOcGeomTransitPiece | kOcGeomNetwork,
  kOcGeomRoadNetwork = 0x8800,
  kOcGeomHighwayNetwork = 0x10800,
  kOcGeomRailNetwork = 0x20800,
  kOcGeomSubwayNetwork = 0x40800,
  kOcGeomPowerNetwork = 0x400800,
  kOcGeomPipeNetwork = 0x800800,
  kOcGeomRoadVehicle = kOcGeomRoad | kOcGeomVehicle,
  kOcGeomRailVehicle = kOcGeomRail | kOcGeomVehicle,
  kOcGeomSubwayVehicle = kOcGeomSubway | kOcGeomVehicle,
  kOcGeomRoadOrHighway = kOcGeomRoad | kOcGeomHighway,
};

// Disaster cleanup can clear every occupant geometry bit with this mask.
enum OcGeometryClearMask : u32 {
  kOcAllGeometryFlags = 0xffffffff,
};

// Shared occupant position packing: X and Z use eleven bits each, altitude eight, rotation two.
enum OcPositionMask : u32 {
  kOcPositionCellValueMask = 0x7ff,
  kOcPositionAltitudeValueMask = 0xff,
  kOcPositionRotationMask = 0xc0000000,
  kOcPositionWithoutX = 0xfffff800,
  kOcPositionWithoutZ = 0xffc007ff,
  kOcPositionWithoutCellCoords = 0xffc00000,
  kOcPositionWithoutAltitude = 0xc03fffff,
  kOcPositionWithoutRotation = 0x3fffffff,
};

// Offsets of the recovered packed position, permission, subtype and membership fields.
enum OcPackedShift : u32 {
  kOcPositionZShift = 11,
  kOcPositionAltitudeShift = 22,
  kOcPositionRotationShift = 30,
  kOcSubtypeShift = 16,
  kOcSimRemoveShift = 24,
  kOcUserRemoveShift = 25,
  kOcMembershipShift = 26,
  kOcMarkShift = 31,
  kOcQuarterTurnAngleShift = 14,
  kOcAltitudeFromMicroShift = 14,
};

// Occupant flags retain the reference word, subtype byte, removal permissions, memberships and mark.
enum OcFlagMask : u32 {
  kOcMembershipMask = 0x7c000000,
  kOcMembershipValueMask = 0x1f,
  kOcFlagsWithoutReferences = 0xffff0000,
  kOcFlagsWithoutSubtypeOrMembership = 0x8300ffff,
  kOcDefaultRemovalFlags = 0x03000000,
  kOcFlagsWithoutSubtype = 0xff00ffff,
  kOcFlagsWithoutSimRemove = 0xfeffffff,
  kOcFlagsWithoutUserRemove = 0xfdffffff,
  kOcFlagsWithoutMembership = 0x83ffffff,
  kOcFlagsWithoutMark = 0x7fffffff,
  kOcFlagSimRemove = 0x01000000,
  kOcFlagUserRemove = 0x02000000,
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtCity;

// Reached cISC3OccupantManagerAnim lifetime subset; this is a distinct interface from ScOccupantManager.
class OcAnimManager : public ScUnknown {
public:
  virtual bool Init(CtCity *city, u32 id) = 0; // SDK cISC3OccupantManagerAnim.h:36; original +12.
  virtual bool Shutdown() = 0;                 // SDK cISC3OccupantManagerAnim.h:37; original +16.

public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual u32 GetCellsInX() = 0;
  virtual u32 GetAnimCellsInX() = 0;
  virtual f32 GetWorldUnitsInX() = 0;
  virtual u32 GetCellsInZ() = 0;
  virtual u32 GetAnimCellsInZ() = 0;
  virtual f32 GetWorldUnitsInZ() = 0;
  virtual u32 GetCellsInY() = 0;
  virtual u32 GetAnimCellsInY() = 0;
  virtual f32 GetWorldUnitsInY() = 0;
  // Retail cISC3OccupantManagerAnim type 0x2A00 and producer establish the const receiver.
  virtual u32 GetSenderId() const = 0;
  virtual bool RegisterChangeReceiver(CtChangeReceiver *pChangeReciever, u32 *senderId, bool post) = 0;
  virtual bool UnregisterChangeReceiver(CtChangeReceiver *pChangeReciever) = 0;
  virtual bool UnregisterAllChangeReceivers() = 0;
  // MSVC reverses each overload group: explicit XYZ insertion72/removal80/movement96 precedes the short form.
  virtual bool InsertOccupant(ScOccupant *, i32, u32, bool) = 0;
  virtual bool InsertOccupant(ScOccupant *, i32, i32, i32, i32, u32, bool) = 0;
  virtual void RemoveOccupant(ScOccupant *) = 0;
  virtual void RemoveOccupant(ScOccupant *, i32, i32, i32) = 0;
  virtual bool RemoveOccupants(GeBounds const *, OcIteratorTest *) = 0;
  virtual void RemoveAllOccupants() = 0;
  virtual bool MoveOccupant(ScOccupant *, CtCoord *) = 0;
  virtual bool MoveOccupant(ScOccupant *, i32, i32, i32, i32, i32, i32) = 0;
  virtual bool RefreshOccupant(ScOccupant *, i32, void *) = 0;
  virtual bool RefreshOccupant(ScOccupant *, i32, i32, i32, i32, void *) = 0;
  virtual bool SetTest(OcIteratorTest *, bool) = 0;
  virtual void ShowOrHideOccupants(OcIteratorTest *) = 0;
  virtual bool ShowOccupant(ScOccupant *, bool) = 0;
  virtual bool ShowOccupant(ScOccupant *, i32, i32, i32, bool) = 0;
  virtual bool IsOccupantVisible(ScOccupant *) = 0;
  virtual bool IsOccupantVisible(ScOccupant *, bool) = 0;
  virtual void SetShouldTick(ScOccupant *, bool) = 0;
  virtual bool GetShouldTick(ScOccupant *) = 0;
  virtual bool IsInBoundsCell(u32, u32) = 0;
  virtual bool IsInBoundsAnimCell(u32, u32) = 0;
  virtual bool IsInBoundsWorld(f32, f32) = 0;
  virtual bool IsInBounds(CtCoord const &) = 0;
  virtual bool IsInBounds(i32, i32, i32) = 0;
  virtual bool GetIterator(OcIterator **) = 0;
  virtual bool GetOccupants(OcIterator **, GeBounds const *, OcIteratorTest *) = 0;
  virtual bool GetOccupantsInVolume(GeBounds const &, ScOccupant **, u32 *) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

struct RsKey;
class GeAttrib;
class GeBuildingType;

// Occupant-cache interface IID6179B578 in the installed SIMINIT virtual order.
class OcAttribCache : public ScUnknown {
public:
  virtual bool Init(CtDefinition *) = 0;
  virtual bool Shutdown() = 0;
  virtual i32 Count() = 0;
  // Preserve the installed effective result and immediate typed output, without asserting nominal PC prototype parity.
  virtual bool GetOccupantAttrib(const RsKey &key, GeAttrib **out) = 0;
  // Keep that observable byte without asserting the original nominal return spelling.
  virtual u8 GetBuildingAttrib(RsKey const &, GeBuildingType **) = 0;
  virtual bool GetBuildingAttrib(u32 type, GeBuildingType **out) = 0;
  virtual bool GetGenericAttrib(RsKey const &, u32, void **) = 0;
  virtual bool GetABuildingOfType(u32, u32, RsKey *) = 0;
  virtual OcCacheIterator *CreateNodeIterator() = 0;
  virtual void DeleteNodeIterator(OcCacheIterator *) = 0;
  virtual void AddAttrib(RsKey const &) = 0;
  virtual bool AddAttribs(RsKeyListInterface const &) = 0;
  virtual void PostOccAttribChangedMessage() = 0;
  // Installed code returns AL; the SDK's pointer-sized spelling remains provisional.
  virtual u8 ModifyOccupantAttrib(RsKey const &, OcOverride *) = 0;
  virtual bool ResetAllModifiedAttribs() = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class SpDefinition;
class ScStringView;

// Original occupant-override virtual order, retaining independently verified signatures.
// MSVC reverses this overload group: the no-argument getter occupies original slot48, indexed getter52.
class OcOverride : public ScUnknown {
public:
  virtual RsKey *Key() const = 0;
  virtual void SetName(ScStringView const &) = 0;
  virtual void SetArchitectName(ScStringView const &) = 0;
  virtual void SetDescription(ScStringView const &) = 0;
  virtual void SetLotSize(u8) = 0;
  virtual void SetSpriteAttribKey(RsKey const &) = 0;
  virtual bool GetName(ScStringView *out) = 0;
  virtual bool GetArchitectName(ScStringView *out) = 0;
  virtual u8 GetLotSize() = 0;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) = 0;
  virtual SpDefinition *GetSpriteAttrib() = 0;
  virtual void SetAuthorSerialNumber(ScStringView *) = 0;
  virtual bool GetAuthorSerialNumber(ScStringView **) = 0;
  virtual void SetGUID(u32) = 0;
  virtual bool GetGUID(u32 *) = 0;
};

class GeAttrib;

// Reached cache iterator view: SIMINIT's20-byte producer has the actual Unknown prefix and a borrowed next attribute.
// Its unconsumed reset operation is not reconstructed here; this compact native declaration is not a vtable overlay.
class OcCacheIterator : public ScUnknown {
public:
  virtual GeAttrib *GetNext() = 0;
};

struct RsKey;
class GeAttrib;

// Installed IID6182EA06 has a distinct occupant-factory subobject. City SDK declarations share these exact bodies.
// MSVC reverses the overload group: attribute+12, key+16. Mutable key input retains explicit pointer spelling.
class OcFactory : public ScUnknown {
public:
  virtual bool CreateOccupant(RsKey *key, u32 iid, void **out) = 0;
  virtual bool CreateOccupant(GeAttrib *attributes, u32 iid, void **out) = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

struct GeBounds;
class OcIteratorTest;
class ScOccupant;

// Original cISC3OccManIterator interface. Nullable bounds and filters retain their original separate arguments.
// Reset, Current and Next expose the iterator-held occupant without introducing another reference acquisition.
class OcIterator : public ScUnknown {
public:
  virtual bool Init(const GeBounds *bounds, OcIteratorTest *test) = 0;
  virtual bool Shutdown() = 0;
  virtual ScOccupant *Reset() = 0;
  virtual ScOccupant *Current() = 0;
  virtual ScOccupant *Next() = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3OccAttribOverRideContainer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

#ifdef GetObject
#undef GetObject
#endif
// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class OcOverrideContainer : public ScUnknown {

public:
  virtual bool Clear() = 0;
  virtual bool GetObject(u32 index, OcOverride **) const = 0;
  virtual bool GetObject(RsKey const &, OcOverride **) = 0;
  virtual bool PutObject(OcOverride *) = 0;
  virtual u32 Size() = 0;
  // Returns if the item was not found
  virtual u32 GetIndexForObject(OcOverride const *) = 0;
};

class CtCity;
class RsStream;
class ScStringView;
class ScVariant;
struct RsKey;

// Original IID431517ED descriptor contract through SerialWrite180. The Windows public type establishes method order,
// receiver constness and scalar widths; old bool/BYTE spelling remains qualified where only T_UCHAR is retained.
class OcPersistInfo : public ScUnknown {
public:
  virtual u8 GetAttribKey(RsKey *out) const = 0;
  virtual u8 GetTypeId(u32 *out) const = 0;
  virtual u8 GetLocationInCells(u32 *x, u32 *z, u32 *altitude) const = 0;
  virtual u8 GetSizeInCells(u32 *first, u32 *second) const = 0;
  virtual u8 GetRotationFlag(u32 *out) const = 0;
  virtual u8 GetCanSimRemoveFlag() const = 0;
  virtual u8 GetCanUserRemoveFlag() const = 0;
  virtual void SetAttribKey(const RsKey &key) = 0;
  virtual void SetTypeId(u32 type) = 0;
  virtual void SetLocationInCells(u32 x, u32 z, u32 altitude) = 0;
  virtual void SetSizeInCells(u32 first, u32 second) = 0;
  virtual void SetRotationFlag(u32 rotation) = 0;
  virtual void SetRemoveFlags(u8 first, u8 second) = 0;
  virtual u32 GetPersistRecordVersion() const = 0;
  virtual u8 DidSameKeyWriteResource() = 0;
  virtual u8 SetSameKeyWroteResource(u8 enabled) = 0;
  virtual u8 HasNameField() const = 0;
  virtual u8 GetNameField(ScStringView *out) const = 0;
  virtual u8 SetNameField(ScStringView *value) = 0;
  virtual u8 HasSimCreateDate() const = 0;
  virtual u8 GetSimCreateDate(i32 *year, i32 *day) const = 0;
  virtual u8 SetSimCreateDate(i32 year, i32 day) = 0;
  virtual u8 HasLayerData1() const = 0;
  virtual u8 GetLayerData1(u32 *out) const = 0;
  virtual u8 SetLayerData1(u32 value) = 0;
  virtual u8 HasLayerData2() const = 0;
  virtual u8 GetLayerData2(u32 *out) const = 0;
  virtual u8 SetLayerData2(u32 value) = 0;
  virtual u32 GetIntDataCount() const = 0;
  virtual u8 GetIntData(u32 key, u32 *out) = 0;
  virtual u8 SetIntData(u32 key, u32 value) = 0;
  virtual u32 GetStringDataCount() const = 0;
  virtual u8 GetStringData(u32 key, ScStringView *out) = 0;
  virtual u8 SetStringData(u32 key, ScStringView *value) = 0;
  virtual u32 GetFloatDataCount() const = 0;
  virtual u8 GetFloatData(u32 key, f32 *out) = 0;
  virtual u8 SetFloatData(u32 key, f32 value) = 0;
  virtual u32 GetTokenCount() const = 0;
  virtual u8 GetToken(u32 key, ScVariant **out) = 0;
  virtual u8 AddToken(u32 key, ScVariant *value) = 0;
  virtual u8 Clear() = 0;
  virtual u8 SerialRead(CtCity *city, u32 codec, RsStream *stream) = 0;
  virtual u8 SerialWrite(CtCity *city, RsStream *stream) = 0;
};

#endif
