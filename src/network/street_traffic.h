#ifndef SC3_NETWORK_STREET_TRAFFIC_H
#define SC3_NETWORK_STREET_TRAFFIC_H

#include <list>
#include <map>
#include <vector>

#include "city/api.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/ge_types.h"
#include "network/street.h"
#include "resources/factory.h"
#include "sprites/sp_tick_manager.h"

// Traffic blocks and vehicle/people paths retain distinct resource type and group identities.
enum NwTrafficResourceKey : u32 {
  kNwTrafficBlockResourceType = 0x625c6226,
  kNwTrafficBlockResourceGroup = 0x825c6289,
  kNwVehiclePathResourceType = 0xc25c74f2,
  kNwVehiclePathResourceGroup = 0xa25c7520,
  kNwPeoplePathResourceType = 0xa20bf670,
  kNwPeoplePathResourceGroup = 0xc20bf6c3,
};

class NwTrafficAttrib;
class RsManager;
class RsStream;
class SpDefView;
struct CtCoord;
struct NwVehicleGraphNode;
struct NwPeopleGraphNode;

// Path resources own parallel coordinate arrays; the vehicle resource also owns distances and pitch data.
class NwPeoplePathAttrib : public RsResource {
public:
  NwPeoplePathAttrib();
  ~NwPeoplePathAttrib() override;
  u8 Shutdown();                         // The original private bool/BYTE spelling is not yet established.
  u8 SerialRead(RsStream *stream);       // Original08975; nominal bool/BYTE return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Nominal bool/BYTE spelling remains unresolved.
  u8 SerialWrite(RsStream *stream);      // Nominal bool/BYTE spelling remains unresolved.
  u8 GetBeginOrientation(); // The original source return spelling remains unresolved.
  u8 GetEndOrientation();   // The original source return spelling remains unresolved.
  u8 loaded;
  i32 pointCount;
  i32 *x, *altitude, *z;
  u8 beginOrientation, endOrientation;
};

class NwVehiclePathAttrib : public RsResource {
public:
  NwVehiclePathAttrib();
  ~NwVehiclePathAttrib() override;
  u8 Shutdown();                    // The original private bool/BYTE spelling is not yet established.
  u8 SerialRead(RsStream *stream);  // Reader and nominal bool/BYTE spelling remain unresolved.
  u8 SerialReadBinary(RsStream *stream); // Nominal bool/BYTE spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Nominal bool/BYTE spelling remains unresolved.
  u8 GetBeginOrientation(); // The original source return spelling remains unresolved.
  u8 GetEndOrientation();   // The original source return spelling remains unresolved.
  u8 loaded;
  i32 pointCount;
  i32 *x, *altitude, *z, *distances;
  u8 beginOrientation, endOrientation, placingOrientation;
  f32 pitch;
  i32 duration;
};

// The original resource factories use the common factory base; their text readers remain unresolved.
class NwPeoplePathAttribFactory : public RsFactory {
public:
  NwPeoplePathAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

class NwVehiclePathAttribFactory : public RsFactory {
public:
  NwVehiclePathAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// An original vehicle edge owns the two signed-byte crossing vectors and retains its path resource.
// Linux tVPathGraphEdge symbols and the Windows constructors establish these as STL vectors.
struct NwVehicleGraphEdge {
  NwVehicleGraphEdge();
  ~NwVehicleGraphEdge();
  i8 targetIndex;
  NwVehicleGraphNode *target;
  u8 direction;
  u32 resourceId;
  NwVehiclePathAttrib *pathAttributes;
  u8 reverse, startOrientation, endOrientation;
  f32 timeScale;
  i8 pathIndex;
  std::vector<i8> peopleCrossings, vehicleCrossings;
};

// The original people edge retains its path resource and owns a signed-byte vehicle-crossing vector.
struct NwPeopleGraphEdge {
  NwPeopleGraphEdge();
  ~NwPeopleGraphEdge();
  i8 targetIndex;
  NwPeopleGraphNode *target;
  u8 direction;
  u32 resourceId;
  NwPeoplePathAttrib *pathAttributes;
  u8 reverse, startOrientation, endOrientation;
  i8 pathIndex;
  std::vector<i8> vehicleCrossings;
};

// Original nodes own their edge vectors; coordinate component meanings remain unresolved.
struct NwVehicleGraphNode {
  NwVehicleGraphNode();
  ~NwVehicleGraphNode();
  NwVehicleGraphEdge *GetInternalEdge();
  NwVehicleGraphEdge *GetNextPath();
  void GetOutgoingEdges(NwVehicleGraphEdge **out, u32 *count); // Discarded nominal return remains unresolved.
  void GetPosition(CtCoord *out); // The discarded nominal source return remains unresolved.
  i8 kind;
  i32 coordinate0, coordinate1, coordinate2;
  std::vector<NwVehicleGraphEdge> edges;
};

struct NwPeopleGraphNode {
  NwPeopleGraphNode();
  ~NwPeopleGraphNode();
  NwPeopleGraphEdge *SelectAnEdge(u8 orientation);
  NwPeopleGraphEdge *SelectAnInternalEdge();
  void GetPosition(CtCoord *out); // The discarded nominal source return remains unresolved.
  i32 coordinate0, coordinate1, coordinate2;
  std::vector<NwPeopleGraphEdge> edges;
};

// The vehicle graph keeps its owning block attributes, nodes and sixteen vectors of borrowed node pointers.
class NwVehicleGraph {
public:
  explicit NwVehicleGraph(NwTrafficAttrib *owner);
  ~NwVehicleGraph();
  u8 Init(); // Original201D8; nominal bool/BYTE spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Original20455; nominal return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Original2066A; nominal return spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Original207C6; nominal return spelling remains unresolved.
  NwVehicleGraphNode *GetNode(i32 x, i32 z, u8 kind);
  // Original2034D/203CD; discarded nominal source returns and class-specific RNG initialization remain unresolved.
  void SelectANodeAndInternalEdge(NwVehicleGraphNode **node, NwVehicleGraphEdge **edge, u8 kind);
  void SelectANodeAndInternalEdgeOfOrientation(
      NwVehicleGraphNode **node, NwVehicleGraphEdge **edge, u8 kind, u8 orientation);
  void DeterminePathIntersections(); // Original1FF93; discarded nominal return remains unresolved.
  // Original20063/20904; private nominal return spellings and intersection arithmetic remain unresolved.
  void GetIntersectingVehiclePaths(const CtCoord &start, const CtCoord &end, std::vector<i8> *crossings, u8 path);
  u8 VehiclePathsIntersect(
      const CtCoord &start, const CtCoord &end, const CtCoord &otherStart, const CtCoord &otherEnd);
  NwTrafficAttrib *owner;
  std::vector<NwVehicleGraphNode> nodes;
  std::vector<NwVehicleGraphNode *> nodesByCell[16];
  u8 pathCount;
};

// The people graph stores its nodes and sixteen borrowed directional pointers into that node vector.
class NwPeopleGraph {
public:
  explicit NwPeopleGraph(NwTrafficAttrib *owner);
  ~NwPeopleGraph();
  u8 Init(); // Original0CBF6; nominal bool/BYTE spelling remains unresolved.
  u8 SerialRead(RsStream *stream);  // Original0CD39; nominal bool/BYTE return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Nominal bool/BYTE return spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Nominal bool/BYTE spelling remains unresolved.
  NwPeopleGraphNode *GetNode(i32 x, i32 z);
  NwPeopleGraphNode *GetNearestNodeAtGivenAltitude(i32 x, i32 altitude, i32 z);
  // The original nominal return is unresolved; writes both crossing vectors through the real graph data.
  void GetIntersectingCrossWalks(const CtCoord &start, const CtCoord &end, std::vector<i8> *crossings, u8 path);
  NwTrafficAttrib *owner;
  std::vector<NwPeopleGraphNode> nodes;
  NwPeopleGraphNode *directions[16];
  i8 pathCount;
};

// The original 48-byte resource owns four corner offsets and the two path-graph receivers.
// Its inherited resource methods use this module's compiled copy of the original resource base.
class NwTrafficAttrib : public RsResource {
public:
  NwTrafficAttrib();
  ~NwTrafficAttrib() override;
  u8 Init();
  u8 Shutdown();
  NwVehicleGraph *GetVPathGraphAttrib();
  NwPeopleGraph *GetPPathGraphAttrib();
  i32 GetCornerOffset(u8 corner);
  // Original167FE; the nominal bool/BYTE return spelling remains unresolved.
  static u8 LinesIntersect(
      const CtCoord &start, const CtCoord &end, const CtCoord &otherStart, const CtCoord &otherEnd);
  u8 SerialRead(RsStream *stream);       // Original1695B; nominal bool/BYTE return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Graph readers and nominal return spelling remain unresolved.
  u8 SerialWrite(RsStream *stream);      // Writer and nominal bool/BYTE spelling remain unresolved.

  i32 cornerOffsets[4];
  u8 flag;
  NwVehicleGraph *vehicleGraph;
  NwPeopleGraph *peopleGraph;
};

// The original block-attribute factory uses the same module-local resource-factory base as path resources.
class NwTrafficAttribFactory : public RsFactory {
public:
  NwTrafficAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// The original person resource retains one animation resource and stores a separate script resource key.
class NwPersonAttrib : public RsResource {
public:
  NwPersonAttrib();
  ~NwPersonAttrib() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 Shutdown();                    // Original0C86C; private bool/BYTE spelling remains unresolved.
  u8 SerialRead(RsStream *stream);       // Original0C6C4; nominal bool/BYTE return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Original0C7AC; nominal bool/BYTE return spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Original0C7FE; callback ordering and return spelling remain unresolved.
  RsResource *animationAttributes;
  RsKey scriptKey;
};

class NwPersonAttribFactory : public RsFactory {
public:
  NwPersonAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// The original vehicle resource retains an animation resource and a script key alongside motion parameters.
class NwVehicleAttrib : public RsResource {
public:
  NwVehicleAttrib();
  ~NwVehicleAttrib() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 Shutdown();      // Original1DAC9; private bool/BYTE spelling remains unresolved.
  RsKey *ScriptKey(); // Borrowed return; pointer versus reference source spelling remains unresolved.
  RsResource *AnimSpriteAttrib();
  i32 Length();
  f32 MaxSpeed();
  u8 SerialRead(RsStream *stream);  // Original1D6BC; nominal return spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Original1D8EA; nominal return spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Original1D9D7; writer and return spelling remain unresolved.
  u8 flag;
  f32 maxSpeed;
  RsResource *animationAttributes;
  RsKey scriptKey;
  i32 length, value0, value1, value2;
};

class NwVehicleAttribFactory : public RsFactory {
public:
  NwVehicleAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// Original script instructions own their argument arrays; the opcode's source signedness remains unresolved.
struct NwScriptInstruction {
  u16 opcode;
  u8 argumentCount;
  u32 *arguments;
};

class NwStreetScript : public RsResource {
public:
  NwStreetScript();
  ~NwStreetScript() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 Shutdown();                   // Original162C5; private bool/BYTE spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Original16186; nominal bool/BYTE return spelling remains unresolved.
  i16 instructionCount;
  NwScriptInstruction *instructions;
};

class NwStreetScriptFactory : public RsFactory {
public:
  NwStreetScriptFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// Original animation action descriptor for a sprite definition's frame sequence.
struct NwSpriteAction {
  u32 action;
  u8 frameCount;
  u16 *frames;
};

class NwAnimSpriteAttrib : public RsResource {
public:
  NwAnimSpriteAttrib();
  ~NwAnimSpriteAttrib() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  NwSpriteAction *GetActionDescriptor(u32 action);
  NwSpriteAction *GetARandomActionDescriptor();
  NwSpriteAction *GetDefaultActionDescriptor();
  SpDefView *GetSpriteRepository();
  u8 Shutdown(); // Original04BE1; nominal bool/BYTE spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Original0477F; nominal bool/BYTE spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Original04972; nominal bool/BYTE spelling remains unresolved.
  u8 SerialWrite(RsStream *stream); // Original04AE0; nominal bool/BYTE spelling remains unresolved.
  SpDefView *repository; // Original IID6100.
  u32 actionCount;
  NwSpriteAction *actions;
};

class NwAnimSpriteAttribFactory : public RsFactory {
public:
  NwAnimSpriteAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// Original train type owns four unsigned-word vectors; Linux symbols identify the vector element type.
struct NwTrainType {
  NwTrainType();
  ~NwTrainType();
  i32 value;
  std::vector<u32> values0, values1, values2, values3;
};

// The installed producer-attribute interface has only the resource-key operation after Unknown.
class NwVehicleProducerAttribInterface : public ScUnknown {
public:
  virtual bool GetResKey(RsKey *out) const = 0;
};

struct NwVehicleProductionEntry {
  u32 weight;
  RsKey key;
};

struct NwPeopleModeEntry {
  u32 threshold;
  RsKey key;
};

// The mode stores the mean population and cell radius used by its people producer.
struct NwPeopleMode {
  u32 threshold, entryCount;
  f32 meanPeople;
  u32 radius;
  NwPeopleModeEntry *entries;
};

// Original mode groups use the game's string and an owned array of modes.
struct NwPeopleModeGroup {
  ScString country;
  u32 modeCount, selectionMethod;
  NwPeopleMode *modes;
};

class NwPeopleProducerAttribInterface : public ScUnknown {
public:
  virtual u32 GetNumberOfModes() = 0;
  virtual u32 GetModeSelectionMethod() = 0; // Original enum spelling remains unresolved.
  virtual NwPeopleMode *GetMode(u32 index) = 0;
  virtual bool GetResKey(RsKey *out) const = 0;
};

// The original resource owns a vector of country groups and exposes the producer and serializable views.
// Its format readers and writer remain pure virtual while their complete contracts are recovered.
class NwPeopleProducerAttrib : public RsResource, public NwPeopleProducerAttribInterface, public RsSerializable {
public:
  NwPeopleProducerAttrib();
  ~NwPeopleProducerAttrib() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetNumberOfModes() override;
  u32 GetModeSelectionMethod() override;
  NwPeopleMode *GetMode(u32 index) override;
  bool GetResKey(RsKey *out) const override;
  u8 Read(RsStream *stream) override;
  u8 Write(RsStream *stream) override;
  u8 Shutdown(); // The private bool/BYTE spelling remains unresolved.

private:
  u8 SerialReadBinary(RsStream *stream); // Original format reader remains unresolved.
  u8 SerialReadText(RsStream *stream); // Original09B35; nominal bool/BYTE spelling remains unresolved.
  std::vector<NwPeopleModeGroup> groups;
  i32 selectedGroup;
};

// The vehicle producer resource owns its weighted vehicle entries.
class NwVehicleProducerAttrib : public RsResource, public NwVehicleProducerAttribInterface {
  friend class NwVehicleProducer;
public:
  NwVehicleProducerAttrib();
  ~NwVehicleProducerAttrib() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetResKey(RsKey *out) const override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  f32 NumberOfVehicles(); // x87 loads the stored float; nominal float/double source spelling remains unresolved.
  u8 Shutdown(); // The private source return spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Private bool/BYTE source spelling remains unresolved.
  u8 SerialReadBinary(RsStream *stream); // Private bool/BYTE source spelling remains unresolved.
  u8 SerialWrite(RsStream *stream);      // Private bool/BYTE source spelling remains unresolved.
  void GetAVehicleType(RsKey *out) const; // Discarded source return remains unresolved.

private:
  f32 vehicleCount;
  u32 entryCount;
  NwVehicleProductionEntry *entries;
};

// The factory forwards serial operations to the corresponding vehicle producer resource.
class NwVehicleProducerAttribFactory : public RsFactory {
public:
  NwVehicleProducerAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

class NwTrainProducerAttrib : public RsResource {
public:
  NwTrainProducerAttrib();
  ~NwTrainProducerAttrib() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 Shutdown();                   // Original17DDA; private bool/BYTE source spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Original17B12; nominal bool/BYTE return spelling remains unresolved.
  u8 loaded;
  i32 minimum, maximum;
  std::vector<NwTrainType> types;
};

class NwTrainProducerAttribFactory : public RsFactory {
public:
  NwTrainProducerAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// Original air-traffic attributes own an unsigned resource-instance list; the scalar's meaning is unresolved.
class NwAirTrafficAttrib : public RsResource {
public:
  NwAirTrafficAttrib();
  ~NwAirTrafficAttrib() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 Shutdown();                   // Original17DDA; private bool/BYTE source spelling remains unresolved.
  u8 SerialRead(RsStream *stream); // Original03E51; nominal bool/BYTE return spelling remains unresolved.
  u32 GetAPlaneResInstId();        // Original03E32; return signedness and empty-list call sites remain unresolved.
  u8 loaded;
  i32 value;
  std::vector<u32> planeIds;
};

class NwAirTrafficAttribFactory : public RsFactory {
public:
  NwAirTrafficAttribFactory();
  bool New(u32 type, u32 iid, void **out) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// Module-local resource state is populated by the original static initialization path, which remains unresolved.
extern RsManager *NwStreetResourceManager;
extern NwTrafficAttrib *NwDefaultTrafficAttributes;

class RsManager;
struct ApIniContext;
class RsStream;
class RsResource;
class ScString;
class NwTrafficAttrib;
class NwHelicopterProducer;
class NwBoatProducer;
class NwAirTrafficAttrib;
class NwAirTrafficManager;
class CtAgentTree;
class NwPeopleProducer;
class NwVehicleProducer;
class NwTrainProducer;
class NwSubwayProducer;
class NwShipProducer;
class NwStrikeCrowdProducer;
class NwTrafficLayer;
class NwPeopleProducerAttrib;
class NwVehicleProducerAttrib;
class NwTrainProducerAttrib;
class CtResidentialLayer;
class DsLayer;
class DsInstance;
class ScDirtBag;
class ScOccupantManager;
class OcAnimManager;
class OcIteratorTest;
class SpTickManager;
class NwPeopleRectFilter;
class NwRoadRectFilter;
class GePortType;
class DsToxicMotion;
struct RsKey;
struct GeCellRect;

// The original layer has these three interface bases. The recovered rectangles and safety grid follow explicit
// unresolved field regions; the remaining tail and lifecycle are not yet reconstructed.
class NwStreetLayer final : public CtStartSimLayer, public CtChangeReceiver, public SpTickable {
  friend class NwStreetManipulator;
public:
  NwStreetLayer();
  ~NwStreetLayer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *context) override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool InitLayer() override;
  bool SetNumZoomLevelsPeopleAreVisible(i32 levels) override;
  bool SetNumZoomLevelsTrafficVisible(i32 levels) override;
  iptr DispatchAirplane(u32 x, u32 z) override;
  bool RedirectAirplane(ScOccupant *plane, u32 x, u32 z) override;
  iptr DispatchHelicopter(u32 startX, u32 startZ, u32 targetX, u32 targetZ) override;
  void RevokeDispatchedAirplane(ScOccupant *plane) override;
  void RevokeDispatchedHelicopter(ScOccupant *helicopter) override;
  NwRiotCrowd *CreateRiotCrowd() override;
  NwParadeBlock *CreateParadeBlock(u32 resource) override;
  u32 GetLayerType() override;
  void DoAnimationTick(u32 elapsed) override;
  void GetAnimationTickPriority(u8 *priority, i32 *interval) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  static u8 GetTextLine(RsStream *stream, ScString *line); // Nominal bool/BYTE return spelling remains unresolved.
  static u8 GetAnimSpriteAttrib(const RsKey &key, RsResource **out);
  static u8 GetPersonAttrib(const RsKey &key, RsResource **out);
  static u8 GetPplPathAttrib(const RsKey &key, RsResource **out);
  static u8 GetVclPathAttrib(const RsKey &key, RsResource **out);
  static u8 GetVehicleAttrib(const RsKey &key, RsResource **out);
  static u8 GetScript(const RsKey &key, RsResource **out); // Private bool/BYTE spelling remains unresolved.
  static NwTrafficAttrib *GetSSTrBlockAttrib(u32 instance);
  RsManager *GetResourceManager(); // Original return is borrowed.
  GeCellRect *GetStrtSimRect(); // Borrowed return; pointer versus reference source spelling remains unresolved.
  u8 GetAnEmergencyVehicleResInstId(u32 *out); // Nominal bool/BYTE spelling remains unresolved.
  static void SC3_CDECL AddEmergencyVehicleResInstId(const ScString &name, const ScString &value, ApIniContext *context);
  static void SC3_CDECL AddKeyVehicleResInstId(const ScString &name, const ScString &value, ApIniContext *context);
  i32 GetBlockDistance(i32 x0, i32 z0, i32 x1, i32 z1);
  u32 IdenticalRects(const GeCellRect &first, const GeCellRect &second); // Source return spelling remains unresolved.
  u8 Contains(const GeCellRect &outer, const GeCellRect &inner); // Source return spelling remains unresolved.
  u8 RectIntersect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out);
  static void SubtractRect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out, i32 *count);
  void ComputeSurroundStrips(const GeCellRect &rect, u32 distance, u32 maxX, u32 maxZ, GeCellRect *out, u32 *count);
  static u8 GetPortAttrib(ScOccupant *occupant, GePortType **out); // Nominal bool/BYTE spelling remains unresolved.
  u8 IsRunway(ScOccupant *occupant); // Nominal bool/BYTE spelling and distinct Windows entry remain unresolved.
  u8 IsTaxiway(ScOccupant *occupant); // Nominal bool/BYTE spelling remains unresolved.
  u8 CellIsInPeopleSimRect(u32 x, u32 z); // Original method and private bool/BYTE spelling remain unresolved.
  u8 CellIsInVehicleSimRect(u32 x, u32 z); // Original private bool/BYTE spelling remains unresolved.
  bool CellIsInBorderOfPeopleSimRect(u32 x, u32 z);
  bool IsPeopleSimRectCellUnsafe(u32 x, u32 z);
  bool IsPeopleSimRectCellClearlySafe(u32 x, u32 z);
  bool DisastersPresent();
  u8 ToxicCloudPresent(u32 x, u32 z) const;
  i32 SafetyValueInPeopleSimRect(i32 x, i32 z);
  void GetSimRectIndices(GeCellRect *rect); // Discarded nominal return remains unresolved.
  bool InCityBounds(u32 x, u32 z);
  void ComputeSurroundRect(const GeCellRect &rect, GeCellRect *out, u32 distance);
  void MakeRectAroundPoint(u32 x, u32 z, GeCellRect *out);
  void GetBorderRects(GeCellRect **out, u32 *count);
  void BeginEmergencyMode(); // The discarded nominal return remains unresolved.
  void EndEmergencyMode(); // View refresh dependencies remain unresolved.

private:
  u8 initialized, unresolved13[3];
  u32 references;
  CtCity *city;
  DsLayer *disasterLayer;
  NwTrafficLayer *trafficLayer;
  CtResidentialLayer *residentialLayer;
  u32 emergencyMode;
  u8 vehicleEmergencyMode;
  u8 unresolved41[3];
  u32 emergencyTimer;
  u8 peopleActive, vehiclesActive, trainsActive, subwayActive;
  u32 maxCellX, maxCellZ;
  i32 maxAnimX, maxAnimY, maxAnimZ;
  ScOccupantManager *surfaceOccupantManager;
  OcAnimManager *animationManager;
  OcIteratorTest *automataFilter;
  OcIteratorTest *peopleFilter, *vehicleFilter;
  OcIteratorTest *peopleRoadFilter;
  OcIteratorTest *railFilter, *subwayFilter;
  OcIteratorTest *nonEmergencyVehicleFilter;
  NwPeopleRectFilter *peopleRectFilter;
  NwRoadRectFilter *roadRectFilter;
  u32 disasterCallbackUsers;
  ScRandom random;
  GeCellRect simulationRect, peopleSimRect, vehicleSimRect;
  u32 peopleOriginX, peopleOriginZ;
  u32 surfaceSender;
  std::map<ScOccupant *, NwPeopleProducer *> peopleScenes;
  u8 unresolvedAfterPeopleScenes[4];
  std::map<ScOccupant *, NwTrainProducer *> trainProducers;
  u8 unresolvedAfterTrainProducers[4];
  std::map<ScOccupant *, NwSubwayProducer *> subwayProducers;
  u8 unresolvedAfterSubwayProducers[4];
  std::map<ScOccupant *, NwShipProducer *> shipProducers;
  u8 unresolvedAfterShipProducers[4];
  NwPeopleProducer *peopleProducer;
  NwVehicleProducer *vehicleProducer;
  NwHelicopterProducer *helicopterProducer;
  NwBoatProducer *boatProducer;
  NwStrikeCrowdProducer *strikeProducer;
  u8 unresolvedBeforeBorders[12]; // Original vector element type at x86 offsets 272 through 283 remains unresolved.
  GeCellRect borderRects[4];
  u32 borderCount;
  i16 safetyValues[70 * 70];
  i32 disasterCount;
  CtLayerManipulator *manipulator;
  GeCellRect viewRect;
  u32 viewCenterX, viewCenterZ;
  i32 currentZoom;
  i32 currentViewType;
  CtAgentTree *agentTypeTree;
  // The original list occupies one x86 word; the current standard library uses two, shifting the following fields.
  std::list<NwAirTrafficManager *> airTrafficManagers;
  SpTickManager *tickManager;
  DsToxicMotion *toxicMotion = nullptr;
  void UpdatePeopleView();
  void UpdateVehicleView();
  void UpdateTrainsView();
  void UpdateSubwayView();
  void UpdatePlanesView();
  void UpdateBoatView();
  void UpdateShipView();
  void UpdateNumberOfBoats(); // The discarded nominal return remains unresolved.
  void UpdateNumberOfHelicopters(); // The discarded nominal return remains unresolved.
  void UpdateHelicopterView(); // Static resource initialization remains unresolved.
  static void DisasterChangeCallback(DsInstance *instance, i32 change, void *context);
  void RegisterDisasterCallback(); // The discarded nominal return remains unresolved.
  void UnregisterDisasterCallback(); // The discarded nominal return remains unresolved.
  void DisasterAdd(DsInstance *instance); // The discarded nominal return remains unresolved.
  void DisasterRemove(DsInstance *instance); // The discarded nominal return remains unresolved.
  void InitDisasterInfo();
  void CreatePeopleScene(const GeCellRect &rect);
  void ErasePeopleScene(const GeCellRect &rect);
  void CreatePeopleSceneForOccupant(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void ErasePeopleSceneForOccupant(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void CreateParadeSpectators(u32 x, u32 z, u32 count) override; // Discarded nominal return remains unresolved.
  void PierAdded(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void PierRemoved(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void RunwayTileAdded(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void RunwayTileRemoved(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void SubwayTileInserted(ScMessage *message); // Discarded nominal return remains unresolved.
  void SubwayTileRemoved(ScMessage *message); // Discarded nominal return remains unresolved.
  void ProcessViewChange(ScMessage *message); // Discarded nominal return remains unresolved.
  static u8 StartBackgroundResLoader(); // Private bool/BYTE return spelling remains unresolved.
  static u8 EndBackgroundResLoader(); // Private bool/BYTE return spelling remains unresolved.
  static u32 SC_THREAD_CALL BackgroundResLoaderThreadFunction(void *argument);
  void ShutdownPeople();
  void ShutdownVehicles(); // Discarded nominal return remains unresolved.
  u8 CreateGlobalPplProducer(); // Private bool/BYTE spelling and static resource initialization remain unresolved.
  u8 CreateGlobalVclProducer(); // Private bool/BYTE spelling and static resource initialization remain unresolved.
  u8 CitySpecificSubSystemsInit(); // Nominal bool/BYTE return spelling remains unresolved.
  u8 CitySpecificSubSystemsShutdown();
  // The original Linux symbols identify each cache as map<unsigned int, cIGZResource *>.
  static std::map<u32, RsResource *> animationAttributes, personAttributes, peoplePaths, vehiclePaths, vehicleAttributes;
  static std::map<u32, RsResource *> scripts;
  static std::map<u32, NwTrafficAttrib *> trafficAttributes;
  static std::vector<u32> emergencyVehicles, keyVehicles;
  static NwAirTrafficAttrib *helicopterAttributes;
  static NwAirTrafficAttrib *airTrafficAttributes;
  static NwTrainProducerAttrib *trainAttributes;
  static NwTrainProducerAttrib *subwayAttributes;
  static u8 backgroundResourceLoadingEnabled;
  static i32 backgroundResourceLoadingCompleted;
  static NwPeopleProducerAttrib *globalPeopleAttributes;
  static NwVehicleProducerAttrib *globalVehicleAttributes;
};

// The manipulator borrows its street-simulation layer.
class NwStreetManipulator final : public CtLayerManipulator {
public:
  explicit NwStreetManipulator(NwStreetLayer *layer);
  ~NwStreetManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
private:
  u32 references;
  NwStreetLayer *layer;
};

class NwTrafficAttrib;
class NwPeoplePath;
class NwPeopleInterface;
class ScDirector;

// The original 24-byte traffic block borrows its attributes and owns two path arrays.
// Path entry and attribute loading remain declared until their original dependencies are recovered.
class NwTrafficBlockObject final : public NwTrafficBlock {
public:
  NwTrafficBlockObject();
  ~NwTrafficBlockObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(const RsKey &key) override;
  u8 Shutdown() override;
  NwVehicle *LastVehicle(i8 path) override;
  u8 EnterVehicle(NwVehiclePath *path, NwVehicle *vehicle, i32 mode, bool force) override;
  void VehicleLeftPath(i8 path, NwVehicle *vehicle) override;
  void BlockPath(NwVehiclePath *path) override;
  void UnblockPath(NwVehiclePath *path) override;
  void DisallowVehicleEntrance() override;
  void AllowVehicleEntrance() override;
  u8 ContainsVehicles() override;
  // Nonvirtual people-path operations; private nominal bool/BYTE and discarded return spellings remain unresolved.
  u8 EnterPerson(NwPeoplePath *path, NwPeopleInterface *person);
  void PersonLeftPath(i8 path, NwPeopleInterface *person);
  NwTrafficAttrib *GetStrtSimAttrib();
  u32 IsBlockedPath(i8 path); // Observed 32-bit return; original nominal return type remains unresolved.

private:
  NwTrafficAttrib *attributes;
  NwVehicle **lastVehicles;
  i8 *peopleCounts;
  u32 references;
  u16 blockedPaths;
  u8 entranceBlocked;
};

// Class225C563D retains the module director once for each outstanding factory reference.
class NwTrafficFactoryObject final : public NwTrafficBlockFactory {
public:
  NwTrafficFactoryObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateStrtSimTrBlock(const RsKey &key, void **out) override;

private:
  u32 references;
};

ScDirector *NwGetStreetDirector();
ScUnknown *NwCreateTrafficFactory();

class NwVehiclePath;
class ScOccupant;
struct CtCoord;

// STRTSIM's vehicle center and true-bounds outputs use the signed two-word point type.
struct ScPointXZSigned {
  i32 x, z;
};

// IID225C7AB0: cISSVehicle, reached through the actual cSSVehicle secondary identity.
// The complete PC order is established by table 1002CCB4 and Linux 9DF80; ignored nominal results are provisional.
class NwVehicle : public ScUnknown {
public:
  // MSVC reverses this overload group in the table: the two-argument form occupies slot12.
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant) = 0;
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant, u8 argument) = 0;
  virtual i32 DistanceToBack(i32 position, NwVehiclePath *path) = 0;
  virtual u8 SetFollowingVehicle(NwVehicle *vehicle) = 0;
  virtual iptr SetVehicleAhead(NwVehicle *vehicle) = 0;
  virtual void FollowingVehicleDisappeared(NwVehicle *vehicle) = 0;
  virtual void VehicleAheadLeftPath(NwVehicle *vehicle) = 0;
  virtual u8 IsVisible() = 0;
  virtual i32 Width() const = 0;
  virtual i32 Length() const = 0;
  virtual iptr SetFollower(NwVehicle *vehicle) = 0;
  virtual iptr SetLeader(NwVehicle *vehicle) = 0;
  virtual void LeaderDisappeared(NwVehicle *vehicle) = 0;
  virtual void FollowerDisappeared(NwVehicle *vehicle) = 0;
  virtual f32 LastMovementDist() = 0;
  virtual void TickFromLeader() = 0;
  virtual iptr LeaderEnteredPath(NwVehiclePath *path) = 0;
  virtual void GetLocationInCellCoords(u32 *x, u32 *z) const = 0;
  virtual void GetTrueBoundingRect(ScPointXZSigned *points) const = 0;
  virtual void GetCenterOfMass(ScPointXZSigned *point) const = 0;
  virtual u8 IsEmergencyVehicle() = 0;
  virtual u8 IsAirVehicle() = 0;
  virtual u32 SetOrientation(u8 orientation) = 0;
  virtual u8 ChangePosition(CtCoord *position) = 0;
};

// Both distinct wake-control IIDs expose their own controller and the same fourth virtual operation.
class NwBoat : public ScUnknown {
public:
  virtual u8 EnableWake(bool enabled) = 0;
};

class NwShip : public ScUnknown {
public:
  virtual u8 EnableWake(bool enabled) = 0;
};

#endif
