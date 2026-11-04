#define _CRT_SECURE_NO_WARNINGS
#include "network/street_traffic.h"
#include <cmath>
#include <cstdio>
#include <map>
#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_tornado.h"
#include "disasters/ds_toxic.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_building_data.h"
#include "geometry/ge_types.h"
#include "network/api.h"
#include "network/nw_street_producer.h"
#include "network/nw_street_vehicle.h"
#include "network/street.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"

NwPeoplePathAttribFactory::NwPeoplePathAttribFactory() {
}

bool NwPeoplePathAttribFactory::New(u32, u32 iid, void **out) {
  NwPeoplePathAttrib *resource = new NwPeoplePathAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwPeoplePathAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwPeoplePathAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwPeoplePathAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwPeoplePathAttrib *>(resource)->SerialWrite(stream) != 0;
}

NwVehiclePathAttribFactory::NwVehiclePathAttribFactory() {
}

bool NwVehiclePathAttribFactory::New(u32, u32 iid, void **out) {
  NwVehiclePathAttrib *resource = new NwVehiclePathAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwVehiclePathAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehiclePathAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwVehiclePathAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehiclePathAttrib *>(resource)->SerialWrite(stream) != 0;
}

// Borrow an existing cached block attribute or load, initialize and publish the requested resource.
// A refused load returns the module's original default attribute without adding it to the cache.
NwTrafficAttrib *NwStreetLayer::GetSSTrBlockAttrib(u32 instance) {
  auto found = trafficAttributes.find(instance);
  if (found != trafficAttributes.end())
    return found->second;
  RsKey key{kNwTrafficBlockResourceType, kNwTrafficBlockResourceGroup, instance};
  void *selected = nullptr;
  if (!NwStreetResourceManager->GetResource(key, IID_RsResource, &selected))
    return NwDefaultTrafficAttributes;
  NwTrafficAttrib *attributes = static_cast<NwTrafficAttrib *>(selected);
  attributes->Init();
  trafficAttributes[instance] = attributes;
  return attributes;
}

// Start with no target node or path resource, no direction flags and two empty crossing vectors.
// The scale field is assigned by the original resource reader before any defined use.
NwVehicleGraphEdge::NwVehicleGraphEdge() {
  targetIndex = -1;
  pathIndex = -1;
  target = nullptr;
  direction = 0;
  resourceId = 0;
  pathAttributes = nullptr;
  reverse = startOrientation = endOrientation = 0;
}

NwVehicleGraphEdge::~NwVehicleGraphEdge() {
  ScRelease(&pathAttributes);
}

NwPeopleGraphEdge::NwPeopleGraphEdge() {
  targetIndex = -1;
  pathIndex = -1;
  target = nullptr;
  direction = 0;
  resourceId = 0;
  pathAttributes = nullptr;
  reverse = startOrientation = endOrientation = 0;
}

NwPeopleGraphEdge::~NwPeopleGraphEdge() {
  ScRelease(&pathAttributes);
}

NwVehicleGraphNode::NwVehicleGraphNode() {
}

NwVehicleGraphNode::~NwVehicleGraphNode() {
}

// Find the first outgoing vehicle edge that leads to another node, or return null when none does.
NwVehicleGraphEdge *NwVehicleGraphNode::GetInternalEdge() {
  for (NwVehicleGraphEdge &edge : edges)
    if (edge.target)
      return &edge;
  return nullptr;
}

// Return a random outgoing vehicle edge, avoiding a random draw for an empty or single-edge node.
NwVehicleGraphEdge *NwVehicleGraphNode::GetNextPath() {
  u32 count = u32(edges.size());
  if (!count)
    return nullptr;
  if (count == 1)
    return &edges[0];
  return &edges[NwVehicleNodeRandom.Bounded(count)];
}

// Write the original city-coordinate ordering from this vehicle node's stored components.
void NwVehicleGraphNode::GetPosition(CtCoord *out) {
  out->x = coordinate0;
  out->z = coordinate2;
  out->altitude = coordinate1;
}

NwPeopleGraphNode::NwPeopleGraphNode() {
}

NwPeopleGraphNode::~NwPeopleGraphNode() {
}

// Pick an outgoing edge while preferring a start orientation different from the previous one.
NwPeopleGraphEdge *NwPeopleGraphNode::SelectAnEdge(u8 orientation) {
  u32 count = u32(edges.size());
  if (!count)
    return nullptr;
  if (count == 1)
    return &edges[0];
  u32 selected = NwPeopleNodeRandom.Bounded(count - 1), index = 0;
  for (NwPeopleGraphEdge &edge : edges) {
    if (edge.startOrientation != orientation) {
      if (index == selected)
        return &edge;
      ++index;
    }
  }
  return &edges[NwPeopleNodeRandom.Bounded(count)];
}

// Search circularly from a random edge for the first one that leads to another people node.
NwPeopleGraphEdge *NwPeopleGraphNode::SelectAnInternalEdge() {
  u32 count = u32(edges.size());
  if (!count)
    return nullptr;
  u32 first = NwPeopleNodeRandom.Bounded(count), index = first;
  do {
    if (edges[index].target)
      return &edges[index];
    if (++index == count)
      index = 0;
  } while (index != first);
  return nullptr;
}

// Write the original city-coordinate ordering from this people node's stored components.
void NwPeopleGraphNode::GetPosition(CtCoord *out) {
  out->x = coordinate0;
  out->z = coordinate2;
  out->altitude = coordinate1;
}

NwVehicleGraph::NwVehicleGraph(NwTrafficAttrib *value) {
  owner = value;
}

NwVehicleGraph::~NwVehicleGraph() {
}

// Read text vehicle-graph nodes and edges, or dispatch the recognized binary graph format.
u8 NwVehicleGraph::SerialRead(RsStream *stream) {
  ScString line;
  if (!NwStreetLayer::GetTextLine(stream, &line))
    return 0;
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  NwStreetLayer::GetTextLine(stream, &line);
  u32 nodeCount = 0;
  std::sscanf(line.ToChar(), "%u", &nodeCount);
  if (nodeCount)
    nodes.reserve(nodeCount);
  for (u32 i = 0; i < nodeCount; ++i) {
    NwVehicleGraphNode node;
    node.kind = 0;
    node.coordinate0 = node.coordinate1 = node.coordinate2 = 0;
    i32 index = 0, kind = 0;
    u32 edgeCount = 0;
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d %d %d %d %d %u", &index, &kind, &node.coordinate0, &node.coordinate1,
        &node.coordinate2, &edgeCount);
    node.kind = static_cast<i8>(kind);
    node.edges.reserve(edgeCount);
    for (u32 j = 0; j < edgeCount; ++j) {
      NwVehicleGraphEdge edge;
      i32 target = 0, direction = 0;
      u32 scale = 0;
      NwStreetLayer::GetTextLine(stream, &line);
      std::sscanf(line.ToChar(), "%d %d %u %u", &target, &direction, &edge.resourceId, &scale);
      if (target != -1)
        target = i32(u32(target) - 1);
      edge.targetIndex = static_cast<i8>(target);
      edge.direction = static_cast<u8>(direction);
      edge.reverse = 0;
      edge.timeScale = static_cast<f32>(f64(scale) * f64(0.00000000222222f));
      node.edges.push_back(edge);
    }
    nodes.push_back(node);
  }
  return 1;
}

// Read binary vehicle-graph nodes and edges, preserving the original stream-call order.
// The original ignores individual read results; initialized locals exclude undefined short-read values.
u8 NwVehicleGraph::SerialReadBinary(RsStream *stream) {
  u32 nodeCount = 0;
  stream->ReadU32(&nodeCount);
  if (nodeCount)
    nodes.reserve(nodeCount);
  for (u32 i = 0; i < nodeCount; ++i) {
    NwVehicleGraphNode node;
    node.kind = 0;
    node.coordinate0 = node.coordinate1 = node.coordinate2 = 0;
    u32 index = 0, edgeCount = 0;
    stream->ReadU32(&index);
    stream->ReadI8(&node.kind);
    stream->ReadI32(&node.coordinate0);
    stream->ReadI32(&node.coordinate1);
    stream->ReadI32(&node.coordinate2);
    stream->ReadU32(&edgeCount);
    node.edges.reserve(edgeCount);
    for (u32 j = 0; j < edgeCount; ++j) {
      NwVehicleGraphEdge edge;
      u32 scale = 0;
      stream->ReadI8(&edge.targetIndex);
      stream->ReadU8(&edge.direction);
      stream->ReadU32(&edge.resourceId);
      stream->ReadU32(&scale);
      if (edge.targetIndex != -1)
        edge.targetIndex = static_cast<i8>(u8(edge.targetIndex) - 1);
      edge.reverse = 0;
      edge.timeScale = static_cast<f32>(f64(scale) * f64(0.00000000222222f));
      node.edges.push_back(edge);
    }
    nodes.push_back(node);
  }
  return 1;
}

// Write the binary graph marker and each node's coordinates and outgoing resource edges.
u8 NwVehicleGraph::SerialWrite(RsStream *stream) {
  stream->WriteU8('B');
  stream->WriteU8('I');
  stream->WriteU8('N');
  stream->WriteU8('\r');
  stream->WriteU32(u32(nodes.size()));
  u32 index = 1;
  for (NwVehicleGraphNode &node : nodes) {
    stream->WriteU32(index++);
    stream->WriteI8(node.kind);
    stream->WriteI32(node.coordinate0);
    stream->WriteI32(node.coordinate1);
    stream->WriteI32(node.coordinate2);
    stream->WriteU32(u32(node.edges.size()));
    for (NwVehicleGraphEdge &edge : node.edges) {
      i8 target = edge.targetIndex == -1 ? i8(-1) : static_cast<i8>(u8(edge.targetIndex) + 1);
      stream->WriteI8(target);
      stream->WriteU8(edge.direction);
      stream->WriteU32(edge.resourceId);
      stream->WriteU32(u32(static_cast<i64>(f64(edge.timeScale) / f64(0.00000000222222f))));
    }
  }
  return 1;
}

// Resolve each vehicle edge's target and retained path resource, then index nodes and calculate crossings.
u8 NwVehicleGraph::Init() {
  pathCount = 0;
  RsKey key{kNwVehiclePathResourceType, kNwVehiclePathResourceGroup, 0};
  for (NwVehicleGraphNode &node : nodes) {
    for (NwVehicleGraphEdge &edge : node.edges) {
      if (edge.targetIndex < 0) {
        edge.target = nullptr;
        edge.pathIndex = -1;
        edge.pathAttributes = nullptr;
        edge.endOrientation = edge.direction;
        edge.startOrientation = u8(edge.direction + 16);
        if (edge.startOrientation >= 32)
          edge.startOrientation = u8(edge.startOrientation - 32);
      } else {
        edge.target = &nodes[edge.targetIndex];
        edge.pathIndex = i8(pathCount++);
        key.instance = edge.resourceId;
        RsResource *resource;
        if (!NwStreetLayer::GetVclPathAttrib(key, &resource))
          return 0;
        edge.pathAttributes = static_cast<NwVehiclePathAttrib *>(resource);
        if (!edge.reverse) {
          edge.startOrientation = edge.pathAttributes->beginOrientation;
          edge.endOrientation = edge.pathAttributes->endOrientation;
        } else {
          edge.startOrientation = u8(edge.pathAttributes->endOrientation + 16);
          if (edge.startOrientation > 32)
            edge.startOrientation = u8(edge.startOrientation - 32);
          edge.endOrientation = u8(edge.pathAttributes->beginOrientation + 16);
          if (edge.endOrientation > 32)
            edge.endOrientation = u8(edge.endOrientation - 32);
        }
      }
    }
    i32 column = i32(u32(node.coordinate0) + 16) / 72, row = i32(u32(node.coordinate2) + 16) / 72;
    nodesByCell[u32(column) * 4 + u32(row)].push_back(&node);
  }
  DeterminePathIntersections();
  return 1;
}

// Record each real vehicle edge's intersecting people and vehicle paths in the original traversal order.
void NwVehicleGraph::DeterminePathIntersections() {
  for (NwVehicleGraphNode &node : nodes) {
    CtCoord start;
    node.GetPosition(&start);
    for (NwVehicleGraphEdge &edge : node.edges) {
      if (edge.targetIndex >= 0) {
        CtCoord end;
        edge.target->GetPosition(&end);
        owner->peopleGraph->GetIntersectingCrossWalks(start, end, &edge.peopleCrossings, u8(edge.pathIndex));
        GetIntersectingVehiclePaths(start, end, &edge.vehicleCrossings, u8(edge.pathIndex));
      }
    }
  }
}

// Start at a random node and wrap once while looking for the requested kind with an internal edge.
void NwVehicleGraph::SelectANodeAndInternalEdge(NwVehicleGraphNode **node, NwVehicleGraphEdge **edge, u8 kind) {
  u32 count = u32(nodes.size());
  if (count) {
    u32 start = NwVehicleGraphRandom.Bounded(count), index = start;
    do {
      NwVehicleGraphNode *candidate = &nodes[index];
      if (u8(candidate->kind) == kind) {
        *edge = candidate->GetInternalEdge();
        if (*edge) {
          *node = candidate;
          return;
        }
      }
      if (++index == count)
        index = 0;
    } while (index != start);
  }
  *node = nullptr;
  *edge = nullptr;
}

// Start at a random node and find the first matching kind whose first internal edge has the requested orientation.
void NwVehicleGraph::SelectANodeAndInternalEdgeOfOrientation(
    NwVehicleGraphNode **node, NwVehicleGraphEdge **edge, u8 kind, u8 orientation) {
  u32 count = u32(nodes.size());
  if (count) {
    u32 start = NwVehicleGraphRandom.Bounded(count), index = start;
    do {
      NwVehicleGraphNode *candidate = &nodes[index];
      if (u8(candidate->kind) == kind) {
        *edge = candidate->GetInternalEdge();
        if (*edge && (*edge)->startOrientation == orientation) {
          *node = candidate;
          return;
        }
      }
      if (++index == count)
        index = 0;
    } while (index != start);
  }
  *node = nullptr;
  *edge = nullptr;
}

// Look up a node of the requested kind in its spatial cell, choosing the closest when the cell has several nodes.
NwVehicleGraphNode *NwVehicleGraph::GetNode(i32 x, i32 z, u8 kind) {
  u32 column = u32(i32(u32(x) + 16) / 72), row = u32(i32(u32(z) + 16) / 72);
  if (column >= 4 || row >= 4)
    return nullptr;
  auto &cell = nodesByCell[4 * column + row];
  if (cell.size() == 1)
    return cell[0]->kind == kind ? cell[0] : nullptr;
  NwVehicleGraphNode *nearest = nullptr;
  i32 best = kNwVehicleNodeDistanceLimit;
  for (NwVehicleGraphNode *node : cell) {
    if (node->kind == kind) {
      u32 dx = u32(x) - u32(node->coordinate0), dz = u32(z) - u32(node->coordinate2);
      i32 distance = i32(dx * dx + dz * dz);
      if (distance < best) {
        best = distance;
        nearest = node;
      }
    }
  }
  return nearest;
}

// Reject segments that begin at the same horizontal position, then use the traffic attribute's intersection operation.
u8 NwVehicleGraph::VehiclePathsIntersect(
    const CtCoord &start, const CtCoord &end, const CtCoord &otherStart, const CtCoord &otherEnd) {
  if (start.x == otherStart.x && start.z == otherStart.z)
    return 0;
  return NwTrafficAttrib::LinesIntersect(start, end, otherStart, otherEnd);
}

// Append other indexed vehicle paths whose endpoints intersect the supplied segment.
void NwVehicleGraph::GetIntersectingVehiclePaths(
    const CtCoord &start, const CtCoord &end, std::vector<i8> *crossings, u8 path) {
  for (NwVehicleGraphNode &node : nodes) {
    CtCoord from;
    node.GetPosition(&from);
    for (NwVehicleGraphEdge &edge : node.edges) {
      if (edge.pathIndex >= 0 && edge.pathIndex != path) {
        CtCoord to;
        edge.target->GetPosition(&to);
        if (VehiclePathsIntersect(start, end, from, to))
          crossings->push_back(edge.pathIndex);
      }
    }
  }
}

NwPeopleGraph::NwPeopleGraph(NwTrafficAttrib *value) : directions{}, pathCount(0) {
  owner = value;
}

NwPeopleGraph::~NwPeopleGraph() {
}

// Resolve people-edge targets and retained path resources, then install each node in its spatial cell.
u8 NwPeopleGraph::Init() {
  RsKey key{kNwPeoplePathResourceType, kNwPeoplePathResourceGroup, 0};
  for (NwPeopleGraphNode &node : nodes) {
    for (NwPeopleGraphEdge &edge : node.edges) {
      if (edge.targetIndex < 0) {
        edge.target = nullptr;
        edge.pathAttributes = nullptr;
        edge.startOrientation = edge.direction >> 2;
        edge.endOrientation = edge.startOrientation;
        edge.direction = 0;
      } else {
        edge.target = &nodes[edge.targetIndex];
        key.instance = edge.resourceId;
        RsResource *resource;
        if (!NwStreetLayer::GetPplPathAttrib(key, &resource))
          return 0;
        edge.pathAttributes = static_cast<NwPeoplePathAttrib *>(resource);
        if (!edge.reverse) {
          edge.startOrientation = edge.pathAttributes->beginOrientation;
          edge.endOrientation = edge.pathAttributes->endOrientation;
        } else {
          edge.startOrientation = u8(edge.pathAttributes->endOrientation + 4);
          if (edge.startOrientation >= 8)
            edge.startOrientation = u8(edge.startOrientation - 8);
          edge.endOrientation = u8(edge.pathAttributes->beginOrientation + 4);
          if (edge.endOrientation >= 8)
            edge.endOrientation = u8(edge.endOrientation - 8);
        }
      }
    }
    i32 column = i32(u32(node.coordinate0) + 16) / 72, row = i32(u32(node.coordinate2) + 16) / 72;
    directions[u32(column) * 4 + u32(row)] = &node;
  }
  return 1;
}

// Select the binary reader for a BIN marker, or append nodes and edges from the original text record.
u8 NwPeopleGraph::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  i32 count = 0;
  std::sscanf(line.ToChar(), "%d", &count);
  if (!count)
    return 1;
  nodes.reserve(u32(count));
  pathCount = 0;
  i32 number = 0, coordinate0 = 0, coordinate1 = 0, coordinate2 = 0, edgeCount = 0;
  i32 target = 0, direction = 0, reverse = 0;
  for (u32 i = 0; i < u32(count); ++i) {
    NwPeopleGraphNode node;
    if (!NwStreetLayer::GetTextLine(stream, &line))
      return 0;
    int converted = std::sscanf(line.ToChar(), "%d %d %d %d %d", &number, &coordinate0, &coordinate1,
        &coordinate2, &edgeCount);
    number = i32(u32(number) - 1);
    if (converted == EOF)
      return 0;
    node.coordinate0 = coordinate0;
    node.coordinate1 = coordinate1;
    node.coordinate2 = coordinate2;
    node.edges.reserve(u32(edgeCount));
    for (u32 j = 0; j < u32(edgeCount); ++j) {
      NwPeopleGraphEdge edge;
      i32 resource = 0;
      NwStreetLayer::GetTextLine(stream, &line);
      std::sscanf(line.ToChar(), "%d %d %d %d", &target, &direction, &resource, &reverse);
      if (target != -1)
        target = i32(u32(target) - 1);
      edge.targetIndex = i8(u8(target));
      edge.direction = u8(direction);
      edge.resourceId = u32(resource);
      edge.reverse = reverse == 1;
      if (edge.targetIndex >= 0 && edge.direction == 1) {
        edge.pathIndex = pathCount;
        pathCount = i8(u8(pathCount) + 1);
      } else {
        edge.pathIndex = -1;
      }
      node.edges.push_back(edge);
    }
    nodes.push_back(node);
  }
  return 1;
}

// Read the binary node and edge records, preserving the original unchecked stream calls and path numbering.
u8 NwPeopleGraph::SerialReadBinary(RsStream *stream) {
  u32 count = u32(uptr(stream));
  stream->ReadU32(&count);
  if (count) {
    nodes.reserve(count);
    pathCount = 0;
    u32 number = 0, edgeCount = 0, reverse = 0;
    i32 coordinate0 = 0, coordinate1 = 0, coordinate2 = 0;
    for (u32 i = 0; i < count; ++i) {
      NwPeopleGraphNode node;
      stream->ReadU32(&number);
      stream->ReadI32(&coordinate0);
      stream->ReadI32(&coordinate1);
      stream->ReadI32(&coordinate2);
      stream->ReadU32(&edgeCount);
      --number;
      node.coordinate0 = coordinate0;
      node.coordinate1 = coordinate1;
      node.coordinate2 = coordinate2;
      node.edges.reserve(edgeCount);
      for (u32 j = 0; j < edgeCount; ++j) {
        NwPeopleGraphEdge edge;
        stream->ReadI8(&edge.targetIndex);
        stream->ReadU8(&edge.direction);
        stream->ReadU32(&edge.resourceId);
        stream->ReadU32(&reverse);
        if (edge.targetIndex != -1)
          edge.targetIndex = i8(u8(edge.targetIndex) - 1);
        edge.reverse = reverse == 1;
        if (edge.targetIndex >= 0 && edge.direction == 1) {
          edge.pathIndex = pathCount;
          pathCount = i8(u8(pathCount) + 1);
        } else {
          edge.pathIndex = -1;
        }
        node.edges.push_back(edge);
      }
      nodes.push_back(node);
    }
  }
  return 1;
}

// Write the binary people-graph marker, nodes and their edges in original order.
u8 NwPeopleGraph::SerialWrite(RsStream *stream) {
  stream->WriteU8('B');
  stream->WriteU8('I');
  stream->WriteU8('N');
  stream->WriteU8('\r');
  stream->WriteU32(u32(nodes.size()));
  u32 number = 1;
  for (NwPeopleGraphNode &node : nodes) {
    stream->WriteU32(number++);
    stream->WriteI32(node.coordinate0);
    stream->WriteI32(node.coordinate1);
    stream->WriteI32(node.coordinate2);
    stream->WriteU32(u32(node.edges.size()));
    for (NwPeopleGraphEdge &edge : node.edges) {
      if (edge.targetIndex == -1)
        stream->WriteI8(-1);
      else
        stream->WriteI8(i8(u8(edge.targetIndex) + 1));
      stream->WriteU8(edge.direction);
      stream->WriteU32(edge.resourceId);
      stream->WriteU32(edge.reverse);
    }
  }
  return 1;
}

// Return the borrowed people node for the requested spatial cell, if the cell is within the graph.
NwPeopleGraphNode *NwPeopleGraph::GetNode(i32 x, i32 z) {
  u32 column = u32(i32(u32(x) + 16) / 72), row = u32(i32(u32(z) + 16) / 72);
  if (column >= 4 || row >= 4)
    return nullptr;
  return directions[4 * column + row];
}

// Select the closest people node at the specified altitude within the original distance threshold.
NwPeopleGraphNode *NwPeopleGraph::GetNearestNodeAtGivenAltitude(i32 x, i32 altitude, i32 z) {
  NwPeopleGraphNode *nearest = nullptr;
  i32 best = kNwPeopleNodeDistanceLimit;
  for (NwPeopleGraphNode &node : nodes) {
    if (node.coordinate1 == altitude) {
      u32 dx = u32(x) - u32(node.coordinate0), dz = u32(z) - u32(node.coordinate2);
      i32 distance = i32(dx * dx + dz * dz);
      if (distance < best) {
        best = distance;
        nearest = &node;
      }
    }
  }
  return nearest;
}

// Append intersecting people-path indices and record the corresponding vehicle path on each intersected edge.
void NwPeopleGraph::GetIntersectingCrossWalks(
    const CtCoord &start, const CtCoord &end, std::vector<i8> *crossings, u8 path) {
  for (NwPeopleGraphNode &node : nodes) {
    CtCoord from;
    node.GetPosition(&from);
    for (NwPeopleGraphEdge &edge : node.edges) {
      if (edge.direction == 1) {
        CtCoord to;
        edge.target->GetPosition(&to);
        if (NwTrafficAttrib::LinesIntersect(start, end, from, to)) {
          crossings->push_back(edge.pathIndex);
          edge.vehicleCrossings.push_back(i8(path));
        }
      }
    }
  }
}

NwTrafficAttrib::NwTrafficAttrib() : vehicleGraph(nullptr), peopleGraph(nullptr) {
}

NwTrafficAttrib::~NwTrafficAttrib() {
  Shutdown();
}

// Borrow the vehicle graph owned by this traffic attribute resource.
NwVehicleGraph *NwTrafficAttrib::GetVPathGraphAttrib() {
  return vehicleGraph;
}

// Borrow the people graph owned by this traffic attribute resource.
NwPeopleGraph *NwTrafficAttrib::GetPPathGraphAttrib() {
  return peopleGraph;
}

i32 NwTrafficAttrib::GetCornerOffset(u8 corner) {
  return cornerOffsets[corner];
}

// Reject segments at different altitudes or with parallel slopes, then check their rounded intersection against both spans.
// Preserve the low DWORD of _ftol, including zero from a masked out-of-range conversion.
u8 NwTrafficAttrib::LinesIntersect(
    const CtCoord &start, const CtCoord &end, const CtCoord &otherStart, const CtCoord &otherEnd) {
  i32 firstHeight = i32(u32(start.altitude) + u32(end.altitude)) / 2;
  i32 secondHeight = i32(u32(otherStart.altitude) + u32(otherEnd.altitude)) / 2;
  i32 heightDifference = i32(u32(firstHeight) - u32(secondHeight));
  if (heightDifference < -200 || heightDifference > 200)
    return 0;
  f32 firstZ = f32(i32(u32(end.z) - u32(start.z)));
  f32 firstX = f32(i32(u32(end.x) - u32(start.x)));
  f64 firstSlope = firstX == 0.0f ? (firstZ > 0.0f ? 100000.0 : -100000.0) : f64(firstZ) / firstX;
  i32 secondZ = i32(u32(otherEnd.z) - u32(otherStart.z));
  f32 secondX = f32(i32(u32(otherEnd.x) - u32(otherStart.x)));
  f64 secondSlope = secondX == 0.0f ? (secondZ > 0 ? 100000.0 : -100000.0) : f64(secondZ) / secondX;
  f64 denominator = firstSlope - secondSlope;
  if (denominator == 0.0)
    return 0;
  i32 zDifference = i32(u32(otherStart.z) - u32(start.z));
  f64 intersection = (f64(start.x) * firstSlope + zDifference - f64(otherStart.x) * secondSlope) / denominator + 0.5;
  i32 x = 0;
  if (intersection >= -0x1p63 && intersection < 0x1p63)
    x = i32(u32(i64(intersection)));
  if (firstX > 0.0f ? x < start.x || x > end.x : x < end.x || x > start.x)
    return 0;
  if (secondX > 0.0f ? x < otherStart.x || x > otherEnd.x : x < otherEnd.x || x > otherStart.x)
    return 0;
  return 1;
}

NwTrafficAttribFactory::NwTrafficAttribFactory() {
}

NwPersonAttrib::NwPersonAttrib() : animationAttributes(nullptr), scriptKey{} {
}

NwPersonAttrib::~NwPersonAttrib() {
  Shutdown();
}

bool NwPersonAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwPersonAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwPersonAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

u8 NwPersonAttrib::Shutdown() {
  ScRelease(&animationAttributes);
  return 1;
}

// Read a binary animation key, retain its resource, and then read the script key through the original stream.
u8 NwPersonAttrib::SerialReadBinary(RsStream *stream) {
  RsKey animationKey;
  RsResource *resource = nullptr;
  stream->ReadKey(&animationKey);
  u8 result = NwStreetLayer::GetAnimSpriteAttrib(animationKey, &resource);
  if (result) {
    animationAttributes = resource;
    return stream->ReadKey(&scriptKey);
  }
  return result;
}

// Read the person resource's animation and script keys, dispatching binary records to their original reader.
u8 NwPersonAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  i32 type = 0, group = 0, instance = 0;
  std::sscanf(line.ToChar(), "%d %d %d", &type, &group, &instance);
  RsKey animationKey{u32(type), u32(group), u32(instance)};
  RsResource *resource;
  if (!NwStreetLayer::GetAnimSpriteAttrib(animationKey, &resource))
    return 0;
  animationAttributes = resource;
  u8 result = NwStreetLayer::GetTextLine(stream, &line);
  type = i32(scriptKey.type);
  group = i32(scriptKey.group);
  instance = i32(scriptKey.instance);
  int converted = std::sscanf(line.ToChar(), "%d %d %d", &type, &group, &instance);
  if (converted >= 1)
    scriptKey.type = u32(type);
  if (converted >= 2)
    scriptKey.group = u32(group);
  if (converted >= 3)
    scriptKey.instance = u32(instance);
  return result;
}

u8 NwPersonAttrib::SerialWrite(RsStream *stream) {
  u8 result = stream->WriteU8('B');
  if (result && (result = stream->WriteU8('I')) && (result = stream->WriteU8('N')))
    result = stream->WriteU8('\r');
  if (!animationAttributes)
    result = 0;
  if (result && (result = stream->WriteKey(animationAttributes->GetKey())))
    result = stream->WriteKey(&scriptKey);
  return result;
}

NwPersonAttribFactory::NwPersonAttribFactory() {
}

bool NwPersonAttribFactory::New(u32, u32 iid, void **out) {
  NwPersonAttrib *resource = new NwPersonAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwPersonAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwPersonAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwPersonAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwPersonAttrib *>(resource)->SerialWrite(stream) != 0;
}

NwVehicleAttrib::NwVehicleAttrib() : animationAttributes(nullptr), scriptKey{} {
}

NwVehicleAttrib::~NwVehicleAttrib() {
  Shutdown();
}

bool NwVehicleAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwVehicleAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwVehicleAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

u8 NwVehicleAttrib::Shutdown() {
  ScRelease(&animationAttributes);
  return 1;
}

// Borrow the stored script key without acquiring another resource reference.
// The original source return's pointer versus reference spelling remains unresolved.
RsKey *NwVehicleAttrib::ScriptKey() {
  return &scriptKey;
}

// Borrow the animation attribute resource retained by this vehicle resource.
RsResource *NwVehicleAttrib::AnimSpriteAttrib() {
  return animationAttributes;
}

i32 NwVehicleAttrib::Length() {
  return length;
}

f32 NwVehicleAttrib::MaxSpeed() {
  return maxSpeed;
}

// Read binary vehicle settings, its animation resource and script key in original field order.
u8 NwVehicleAttrib::SerialReadBinary(RsStream *stream) {
  u8 flagByte = static_cast<u8>(uptr(stream) >> 24);
  u8 result = stream->ReadU8(&flagByte);
  flag = flagByte == 1;
  if (!result)
    return result;
  if (!(result = stream->ReadI32(&value0)) || !(result = stream->ReadI32(&value1)) ||
      !(result = stream->ReadI32(&value2)) || !(result = stream->ReadI32(&length)))
    return result;
  length = static_cast<i32>(u32(length) * 2);
  RsKey animationKey;
  result = stream->ReadKey(&animationKey);
  if (!result)
    return result;
  RsResource *resource = nullptr;
  result = NwStreetLayer::GetAnimSpriteAttrib(animationKey, &resource);
  if (!result)
    return result;
  if (resource)
    animationAttributes = resource;
  result = stream->ReadKey(&scriptKey);
  if (!result)
    return result;
  u32 speedValue;
  result = stream->ReadU32(&speedValue);
  if (result)
    maxSpeed = static_cast<f32>(f64(speedValue) * f64(0.00000000222222f));
  return result;
}

// Write the binary vehicle fields, resource keys and scaled speed, stopping on the first failed write.
u8 NwVehicleAttrib::SerialWrite(RsStream *stream) {
  u8 result = stream->WriteU8('B');
  if (!(result = result && stream->WriteU8('I')) || !(result = stream->WriteU8('N')) ||
      !(result = stream->WriteU8('\r')) || !(result = stream->WriteU8(flag)) ||
      !(result = stream->WriteI32(value0)) || !(result = stream->WriteI32(value1)) ||
      !(result = stream->WriteI32(value2)) || !(result = stream->WriteI32(length / 2)) ||
      !(result = stream->WriteKey(animationAttributes->GetKey())) ||
      !(result = stream->WriteKey(&scriptKey)))
    return result;
  i64 speed = static_cast<i64>(f64(maxSpeed) / f64(0.00000000222222f));
  return stream->WriteU32(static_cast<u32>(speed));
}

// Read a vehicle resource from its text fields or dispatch a binary record.
u8 NwVehicleAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  i32 flagValue;
  if (std::sscanf(line.ToChar(), "%d", &flagValue) != 1)
    return 0;
  flag = flagValue != 0;
  NwStreetLayer::GetTextLine(stream, &line);
  if (std::sscanf(line.ToChar(), "%d %d %d", &value0, &value1, &value2) != 3)
    return 0;
  if (!NwStreetLayer::GetTextLine(stream, &line) || std::sscanf(line.ToChar(), "%d", &length) != 1)
    return 0;
  length = static_cast<i32>(u32(length) * 2);
  if (!NwStreetLayer::GetTextLine(stream, &line))
    return 0;
  i32 type = 0, group = 0, instance = 0;
  if (std::sscanf(line.ToChar(), "%d %d %d", &type, &group, &instance) != 3)
    return 0;
  RsKey animationKey{u32(type), u32(group), u32(instance)};
  RsResource *resource;
  if (!NwStreetLayer::GetAnimSpriteAttrib(animationKey, &resource))
    return 0;
  animationAttributes = resource;
  if (!NwStreetLayer::GetTextLine(stream, &line))
    return 0;
  type = static_cast<i32>(scriptKey.type);
  group = static_cast<i32>(scriptKey.group);
  instance = static_cast<i32>(scriptKey.instance);
  i32 converted = std::sscanf(line.ToChar(), "%d %d %d", &type, &group, &instance);
  if (converted >= 1)
    scriptKey.type = u32(type);
  if (converted >= 2)
    scriptKey.group = u32(group);
  if (converted >= 3)
    scriptKey.instance = u32(instance);
  if (converted != 3 || !NwStreetLayer::GetTextLine(stream, &line))
    return 0;
  i32 speedValue;
  if (std::sscanf(line.ToChar(), "%d", &speedValue) != 1)
    return 0;
  maxSpeed = static_cast<f32>(f64(u32(speedValue)) * f64(0.00000000222222f));
  return 1;
}

// Copy up to the caller's requested count of borrowed outgoing edges.
void NwVehicleGraphNode::GetOutgoingEdges(NwVehicleGraphEdge **out, u32 *count) {
  if (edges.size() < *count)
    *count = static_cast<u32>(edges.size());
  for (u32 i = 0; i < *count; ++i)
    out[i] = &edges[i];
}

NwVehicleAttribFactory::NwVehicleAttribFactory() {
}

bool NwVehicleAttribFactory::New(u32, u32 iid, void **out) {
  NwVehicleAttrib *resource = new NwVehicleAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwVehicleAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehicleAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwVehicleAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehicleAttrib *>(resource)->SerialWrite(stream) != 0;
}

NwStreetScript::NwStreetScript() : instructionCount(0), instructions(nullptr) {
}

NwStreetScript::~NwStreetScript() {
  Shutdown();
}

bool NwStreetScript::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwStreetScript::DebugClassTag(ScStringView *) {
  return false;
}

bool NwStreetScript::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

// Release every instruction's argument array, then the instruction array, leaving the stored count unchanged.
u8 NwStreetScript::Shutdown() {
  if (instructions) {
    for (i32 i = 0; i < instructionCount; ++i)
      if (instructions[i].arguments)
        delete[] instructions[i].arguments;
    delete[] instructions;
    instructions = nullptr;
  }
  return 1;
}

// Read the instruction count, each opcode and its argument lines, including the original separator line.
u8 NwStreetScript::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  i32 count = instructionCount;
  if (std::sscanf(line.ToChar(), "%d", &count) == 1)
    instructionCount = i16(u16(count));
  instructions = new NwScriptInstruction[instructionCount];
  i32 opcode = 0, argumentCount = 0;
  for (i32 i = 0; i < instructionCount; ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d %d", &opcode, &argumentCount);
    NwScriptInstruction &instruction = instructions[i];
    instruction.opcode = u16(opcode);
    instruction.argumentCount = u8(argumentCount);
    instruction.arguments = instruction.argumentCount ? new u32[instruction.argumentCount]() : nullptr;
    for (u32 j = 0; j < instruction.argumentCount; ++j) {
      NwStreetLayer::GetTextLine(stream, &line);
      i32 value;
      if (std::sscanf(line.ToChar(), "%d", &value) == 1)
        instruction.arguments[j] = u32(value);
    }
    NwStreetLayer::GetTextLine(stream, &line);
  }
  return 1;
}

NwStreetScriptFactory::NwStreetScriptFactory() {
}

bool NwStreetScriptFactory::New(u32, u32 iid, void **out) {
  NwStreetScript *resource = new NwStreetScript;
  return resource->QueryInterface(iid, out);
}

bool NwStreetScriptFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwStreetScript *>(resource)->SerialRead(stream) != 0;
}

bool NwStreetScriptFactory::SerialWrite(RsResource *, RsStream *) {
  return false;
}

NwAnimSpriteAttrib::NwAnimSpriteAttrib() : repository(nullptr), actionCount(0), actions(nullptr) {
}

NwAnimSpriteAttrib::~NwAnimSpriteAttrib() {
  Shutdown();
}

bool NwAnimSpriteAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwAnimSpriteAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwAnimSpriteAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

// Borrow the first action descriptor with the requested identifier, or return null when none matches.
NwSpriteAction *NwAnimSpriteAttrib::GetActionDescriptor(u32 action) {
  for (u32 i = 0; i < actionCount; ++i)
    if (actions[i].action == action)
      return &actions[i];
  return nullptr;
}

// Draw an action index from the animation class's random stream and borrow its corresponding descriptor.
NwSpriteAction *NwAnimSpriteAttrib::GetARandomActionDescriptor() {
  u32 index = NwAnimationRandom.Bounded(actionCount);
  return index ? actions + index : actions;
}

// Borrow action one when present, otherwise return the beginning of the action array.
NwSpriteAction *NwAnimSpriteAttrib::GetDefaultActionDescriptor() {
  NwSpriteAction *selected = GetActionDescriptor(1);
  return selected ? selected : actions;
}

// Borrow the retained sprite-definition interface without changing its reference count.
SpDefView *NwAnimSpriteAttrib::GetSpriteRepository() {
  return repository;
}

// Free action frames, release all demanded sprite frames and release both retained repository identities.
u8 NwAnimSpriteAttrib::Shutdown() {
  if (actions) {
    for (u32 i = 0; i < actionCount; ++i)
      delete[] actions[i].frames;
    delete[] actions;
    actions = nullptr;
    u16 frames = repository->SprAttGetTotalFrameCount();
    for (u16 i = 0; i < frames; ++i)
      repository->ReleaseFrame(i);
    RsResource *resource = nullptr;
    repository->QueryInterface(IID_RsResource, reinterpret_cast<void **>(&resource));
    repository->Release();
    repository = nullptr;
    if (resource)
      resource->Release();
  }
  return 1;
}

// Load the sprite definition and its text action sequences, or dispatch a binary animation record.
// Stored animation indices use the repository's animation-set stride, independently of its zoom-set count.
u8 NwAnimSpriteAttrib::SerialRead(RsStream *stream) {
  if (!NwStreetResourceManager)
    return 0;
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  i32 type = 0, group = 0, instance = 0;
  std::sscanf(line.ToChar(), "%d %d %d", &type, &group, &instance);
  RsKey resourceKey{u32(type), u32(group), u32(instance)};
  if (!NwStreetResourceManager->GetResource(resourceKey, IID_SpDefView, reinterpret_cast<void **>(&repository)) ||
      !repository)
    return 0;
  u16 total = repository->SprAttGetTotalFrameCount();
  for (u16 i = 0; i < total; ++i)
    repository->DemandFrame(i);
  u8 result = NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%u", &actionCount);
  actions = new NwSpriteAction[actionCount]();
  for (u32 i = 0; i < actionCount; ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    result = NwStreetLayer::GetTextLine(stream, &line);
    i32 action = 0, frames = 0;
    std::sscanf(line.ToChar(), "%d %d", &action, &frames);
    actions[i].action = u32(action);
    actions[i].frameCount = static_cast<u8>(frames);
    actions[i].frames = new u16[actions[i].frameCount]();
    for (u32 j = 0; j < actions[i].frameCount; ++j) {
      result = NwStreetLayer::GetTextLine(stream, &line);
      i32 frameValue = 0;
      std::sscanf(line.ToChar(), "%d", &frameValue);
      actions[i].frames[j] = u16(u32(frameValue) * repository->GetAnimationSetCount());
    }
  }
  return result;
}

// Load a binary sprite key and action sequences, stopping record operations after their first failure.
u8 NwAnimSpriteAttrib::SerialReadBinary(RsStream *stream) {
  if (!NwStreetResourceManager)
    return 0;
  RsKey resourceKey;
  u8 result = stream->ReadKey(&resourceKey);
  if (!result)
    return result;
  result = NwStreetResourceManager->GetResource(resourceKey, IID_SpDefView, reinterpret_cast<void **>(&repository));
  if (!result || !repository)
    return 0;
  u16 total = repository->SprAttGetTotalFrameCount();
  for (u16 i = 0; i < total; ++i)
    if (result)
      result = repository->DemandFrame(i);
  if (result)
    result = stream->ReadU32(&actionCount);
  actions = new NwSpriteAction[actionCount]();
  for (u32 i = 0; i < actionCount; ++i) {
    if (result) {
      result = stream->ReadU32(&actions[i].action);
      if (result)
        result = stream->ReadU8(&actions[i].frameCount);
    }
    actions[i].frames = new u16[actions[i].frameCount]();
    for (u32 j = 0; j < actions[i].frameCount; ++j) {
      if (result) {
        u16 frameValue = 0;
        result = stream->ReadU16(&frameValue);
        if (result)
          actions[i].frames[j] = u16(u32(frameValue) * repository->GetAnimationSetCount());
      }
    }
  }
  return result;
}

// Write the sprite key and binary action sequences, stopping record operations after their first failure.
// Convert expanded frame indices back through the same animation-set stride used by both readers.
u8 NwAnimSpriteAttrib::SerialWrite(RsStream *stream) {
  RsKey resourceKey;
  repository->GetResourceKey(&resourceKey);
  u8 result = stream->WriteU8('B');
  result = result && stream->WriteU8('I') && stream->WriteU8('N') && stream->WriteU8('\r') &&
      stream->WriteKey(&resourceKey) && stream->WriteU32(actionCount);
  for (u32 i = 0; i < actionCount; ++i) {
    if (result) {
      result = stream->WriteU32(actions[i].action);
      if (result)
        result = stream->WriteU8(actions[i].frameCount);
    }
    for (u32 j = 0; j < actions[i].frameCount; ++j)
      if (result)
        result = stream->WriteU16(u16(actions[i].frames[j] / repository->GetAnimationSetCount()));
  }
  return result;
}

NwAnimSpriteAttribFactory::NwAnimSpriteAttribFactory() {
}

bool NwAnimSpriteAttribFactory::New(u32, u32 iid, void **out) {
  NwAnimSpriteAttrib *resource = new NwAnimSpriteAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwAnimSpriteAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwAnimSpriteAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwAnimSpriteAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwAnimSpriteAttrib *>(resource)->SerialWrite(stream) != 0;
}

NwTrainType::NwTrainType() {
}

NwTrainType::~NwTrainType() {
}

NwPeopleProducerAttrib::NwPeopleProducerAttrib() : selectedGroup(-1) {
}

NwPeopleProducerAttrib::~NwPeopleProducerAttrib() {
  Shutdown();
}

// Publish the people-producer or serializable view, and delegate other identities to the resource base.
bool NwPeopleProducerAttrib::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwPeopleProducerAttribInterface, NwPeopleProducerAttribInterface);
  HANDLE_QUERY_INTERFACE(IID_RsSerializable, RsSerializable);
  return RsResource::QueryInterface(iid, out);
}

u32 NwPeopleProducerAttrib::AddRef() {
  return RsResource::AddRef();
}

u32 NwPeopleProducerAttrib::Release() {
  return RsResource::Release();
}

u32 NwPeopleProducerAttrib::GetNumberOfModes() {
  return groups[selectedGroup].modeCount;
}

u32 NwPeopleProducerAttrib::GetModeSelectionMethod() {
  return groups[selectedGroup].selectionMethod;
}

// Borrow the indexed mode from the selected country group, or return null when out of range.
NwPeopleMode *NwPeopleProducerAttrib::GetMode(u32 index) {
  NwPeopleModeGroup &group = groups[selectedGroup];
  return index < group.modeCount ? group.modes + index : nullptr;
}

bool NwPeopleProducerAttrib::GetResKey(RsKey *out) const {
  *out = key;
  return true;
}

// Read the format tag and dispatch the original binary or text reader when recognized.
u8 NwPeopleProducerAttrib::Read(RsStream *stream) {
  u32 tag = 0;
  if (!stream->ReadU32(&tag))
    return 0;
  if (tag == kRsTextAttributeTag)
    return SerialReadText(stream);
  if (tag == kRsBinaryAttributeTag)
    return SerialReadBinary(stream);
  return 1;
}

// Read text country groups, modes and weighted resource keys into the original producer-attribute arrays.
u8 NwPeopleProducerAttrib::SerialReadText(RsStream *stream) {
  ScString line;
  if (!NwStreetLayer::GetTextLine(stream, &line))
    return 0;
  u32 countryCount = 1;
  bool named = line.BeginsWith("NumCountries:", 13);
  if (named)
    std::sscanf(line.ToChar(), "NumCountries:%u", &countryCount);
  for (u32 groupIndex = 0; groupIndex < countryCount; ++groupIndex) {
    NwPeopleModeGroup group;
    group.modeCount = 0;
    group.selectionMethod = 0;
    group.modes = nullptr;
    if (named) {
      if (!NwStreetLayer::GetTextLine(stream, &line))
        return 0;
      char country[1024] = {};
      std::sscanf(line.ToChar(), "Country:%1023s", country);
      group.country.FromChar(country);
      if (!NwStreetLayer::GetTextLine(stream, &line))
        return 0;
    } else {
      group.country.FromChar("def");
    }
    std::sscanf(line.ToChar(), "NumModes: %u", &group.modeCount);
    group.modes = new NwPeopleMode[group.modeCount]();
    u32 cumulativeMode = kNwCumulativeWeightBeforeFirst;
    for (u32 i = 0; i < group.modeCount; ++i) {
      NwPeopleMode &mode = group.modes[i];
      u32 selection = 0;
      i32 mean = 0;
      NwStreetLayer::GetTextLine(stream, &line);
      std::sscanf(line.ToChar(), "%u %u %d %u", &selection, &mode.entryCount, &mean, &mode.radius);
      mode.meanPeople = static_cast<f32>(f64(mean) * 0.001);
      if (i == 0)
        group.selectionMethod = selection >= kNwPeopleDateSelectionThreshold ? 1 : 0;
      if (group.selectionMethod == 1) {
        u32 startMonth = selection / kNwPeopleDateStartMonthScale,
            startDay = (selection % kNwPeopleDateStartMonthScale) / 10000;
        u32 endMonth = (selection % 10000) / 100, endDay = selection % 100;
        mode.threshold = (startMonth << 24) + (startDay << 16) + (endMonth << 8) + endDay;
      } else {
        cumulativeMode += selection;
        mode.threshold = cumulativeMode;
      }
      mode.entries = new NwPeopleModeEntry[mode.entryCount]();
      u32 cumulativeEntry = kNwCumulativeWeightBeforeFirst;
      for (u32 j = 0; j < mode.entryCount; ++j) {
        u32 weight = 0;
        NwPeopleModeEntry &entry = mode.entries[j];
        NwStreetLayer::GetTextLine(stream, &line);
        std::sscanf(line.ToChar(), "%u %u %u %u", &weight, &entry.key.type, &entry.key.group, &entry.key.instance);
        cumulativeEntry += weight;
        entry.threshold = cumulativeEntry;
      }
      NwStreetLayer::GetTextLine(stream, &line);
    }
    groups.push_back(group);
  }
  return 1;
}

// Read every binary country group, retaining matching groups and their cumulative mode and resource thresholds.
// Resolve the language call before querying preferences, as in the original callback order.
u8 NwPeopleProducerAttrib::SerialReadBinary(RsStream *stream) {
  u32 groupCount = 0;
  u8 result = stream->ReadU32(&groupCount);
  ScString countryCode;
  ApPreferences *preferences = NwGetStreetPreferences();
  ScLanguageManager *language = NwGetStreetLanguageManager();
  language->GetISO3166CountryCodeFromCountryID(&countryCode, u32(preferences->GetCountry()));
  ScString country;
  bool found = false;
  for (u32 groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
    stream->ReadString(&country);
    bool holiday = country.EndsWith(":holidays", 9);
    if (holiday)
      country.Erase(country.Strlen() - 9, 9);
    bool selected = country.IsEqual(&countryCode) || (country.Strlen() == 3 && country.BeginsWith("def", 3) && !found);
    if (selected && holiday) {
      CtCity *city = NwGetStreetCity();
      selected = city && city->AreHolidayFeaturesEnabled();
    }
    if (selected)
      found = true;
    NwPeopleModeGroup group;
    group.modeCount = 0;
    group.selectionMethod = 0;
    group.modes = nullptr;
    result = stream->ReadU32(&group.modeCount);
    if (selected)
      group.modes = new NwPeopleMode[group.modeCount];
    u32 cumulativeMode = kNwCumulativeWeightBeforeFirst;
    for (u32 modeIndex = 0; modeIndex < group.modeCount; ++modeIndex) {
      u32 threshold = 0, count = 0, radius = 0;
      f32 mean = 0;
      result = stream->ReadU32(&threshold);
      stream->ReadU32(&count);
      stream->ReadF32(&mean);
      stream->ReadU32(&radius);
      NwPeopleMode *mode = selected ? &group.modes[modeIndex] : nullptr;
      if (mode) {
        mode->entryCount = count;
        mode->meanPeople = mean;
        mode->radius = radius;
        if (!modeIndex)
          group.selectionMethod = threshold >= kNwPeopleDateSelectionThreshold ? 1 : 0;
        if (group.selectionMethod == 0) {
          cumulativeMode += threshold;
          mode->threshold = cumulativeMode;
        } else {
          mode->threshold = threshold;
        }
        mode->entries = new NwPeopleModeEntry[count];
      }
      u32 cumulativeEntry = kNwCumulativeWeightBeforeFirst;
      for (u32 entryIndex = 0; entryIndex < count; ++entryIndex) {
        u32 weight = 0;
        RsKey resourceKey;
        stream->ReadU32(&weight);
        stream->ReadKey(&resourceKey);
        cumulativeEntry += weight;
        if (mode) {
          mode->entries[entryIndex].threshold = cumulativeEntry;
          mode->entries[entryIndex].key = resourceKey;
        }
      }
    }
    if (selected) {
      selectedGroup = i32(groups.size());
      groups.push_back(group);
    }
  }
  return result;
}

// Write country groups, modes and their resource keys using the original cumulative-threshold differences.
u8 NwPeopleProducerAttrib::Write(RsStream *stream) {
  if (!stream->WriteU8('B') || !stream->WriteU8('I') || !stream->WriteU8('N') || !stream->WriteU8('!') ||
      !stream->WriteU32(u32(groups.size())))
    return 0;
  u32 previousMode = kNwCumulativeWeightBeforeFirst;
  for (usize groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    NwPeopleModeGroup &group = groups[groupIndex];
    if (!stream->WriteString(&group.country) || !stream->WriteU32(group.modeCount))
      return 0;
    for (u32 modeIndex = 0; modeIndex < group.modeCount; ++modeIndex) {
      NwPeopleMode &current = group.modes[modeIndex];
      if (group.selectionMethod == 0) {
        u32 delta = current.threshold - previousMode;
        previousMode = current.threshold;
        if (!stream->WriteU32(delta))
          return 0;
      } else if (group.selectionMethod == 1 && !stream->WriteU32(current.threshold)) {
        return 0;
      }
      if (!stream->WriteU32(current.entryCount) || !stream->WriteF32(current.meanPeople) ||
          !stream->WriteU32(current.radius))
        return 0;
      u32 previousEntry = kNwCumulativeWeightBeforeFirst;
      for (u32 entryIndex = 0; entryIndex < current.entryCount; ++entryIndex) {
        NwPeopleModeEntry &entry = current.entries[entryIndex];
        u32 delta = entry.threshold - previousEntry;
        previousEntry = entry.threshold;
        if (!stream->WriteU32(delta) || !stream->WriteKey(&entry.key))
          return 0;
      }
    }
  }
  return 1;
}

// Free each mode's entries, followed by the mode array of each country group.
u8 NwPeopleProducerAttrib::Shutdown() {
  for (usize groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    NwPeopleModeGroup &group = groups[groupIndex];
    if (group.modes) {
      for (u8 modeIndex = 0; modeIndex < group.modeCount; ++modeIndex) {
        delete[] group.modes[modeIndex].entries;
        group.modes[modeIndex].entries = nullptr;
      }
      delete[] group.modes;
      group.modes = nullptr;
    }
  }
  return 1;
}

NwVehicleProducerAttrib::NwVehicleProducerAttrib() : vehicleCount(0), entryCount(0), entries(nullptr) {
}

NwVehicleProducerAttrib::~NwVehicleProducerAttrib() {
  Shutdown();
}

bool NwVehicleProducerAttrib::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerAttrib, NwVehicleProducerAttribInterface);
  return RsResource::QueryInterface(iid, out);
}

u32 NwVehicleProducerAttrib::AddRef() {
  return RsResource::AddRef();
}

u32 NwVehicleProducerAttrib::Release() {
  return RsResource::Release();
}

bool NwVehicleProducerAttrib::GetResKey(RsKey *out) const {
  *out = key;
  return true;
}

bool NwVehicleProducerAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwVehicleProducerAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwVehicleProducerAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

f32 NwVehicleProducerAttrib::NumberOfVehicles() {
  return vehicleCount;
}

// Destroy the loaded producer entries and clear their pointer.
u8 NwVehicleProducerAttrib::Shutdown() {
  delete[] entries;
  entries = nullptr;
  return 1;
}

// Read the binary vehicle count and weighted resource entries after the format marker has been consumed.
u8 NwVehicleProducerAttrib::SerialReadBinary(RsStream *stream) {
  if (!stream->ReadF32(&vehicleCount) || !stream->ReadU32(&entryCount))
    return 0;
  if (!entryCount || !(vehicleCount > 0.0f))
    return 0;
  entries = new NwVehicleProductionEntry[entryCount];
  for (u32 i = 0; i < entryCount; ++i) {
    // The original multiplies this field even on failure; zero avoids reading untouched bytes.
    entries[i].weight = 0;
    bool result = stream->ReadU32(&entries[i].weight);
    if (result)
      result = stream->ReadKey(&entries[i].key);
    entries[i].weight *= 10000;
    if (!result)
      return 0;
  }
  return 1;
}

// Write the binary marker, vehicle count and weighted resource keys in their original record order.
u8 NwVehicleProducerAttrib::SerialWrite(RsStream *stream) {
  if (!stream->WriteU8('B') || !stream->WriteU8('I') || !stream->WriteU8('N') || !stream->WriteU8('\r') ||
      !stream->WriteF32(vehicleCount) || !stream->WriteU32(entryCount))
    return 0;
  for (u32 i = 0; i < entryCount; ++i) {
    if (!stream->WriteU32(entries[i].weight / 10000) || !stream->WriteKey(&entries[i].key))
      return 0;
  }
  return 1;
}

void NwVehicleProducerAttrib::GetAVehicleType(RsKey *out) const {
  *out = entries[NwVehicleProducerRandom.Bounded(entryCount)].key;
}

NwVehicleProducerAttribFactory::NwVehicleProducerAttribFactory() {
}

// Read the vehicle-production count and weighted resource keys from text or a binary record.
u8 NwVehicleProducerAttrib::SerialRead(RsStream *stream) {
  ScString line;
  u8 result = NwStreetLayer::GetTextLine(stream, &line);
  if (!result)
    return result;
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  i32 countValue = 0, entriesValue = static_cast<i32>(entryCount);
  i32 converted = std::sscanf(line.ToChar(), "%d %d", &countValue, &entriesValue);
  if (converted >= 2)
    entryCount = static_cast<u32>(entriesValue);
  vehicleCount = static_cast<f32>(f64(countValue) * 0.001);
  if (converted != 2 || !entryCount || vehicleCount <= 0.0f)
    return 0;
  entries = new NwVehicleProductionEntry[entryCount];
  for (u32 i = 0; i < entryCount; ++i)
    entries[i].weight = 0;
  result = 1;
  for (u32 i = 0; i < entryCount; ++i) {
    if (!result)
      continue;
    result = NwStreetLayer::GetTextLine(stream, &line);
    if (!result)
      continue;
    i32 weight = static_cast<i32>(entries[i].weight), type = static_cast<i32>(entries[i].key.type);
    i32 group = static_cast<i32>(entries[i].key.group), instance = static_cast<i32>(entries[i].key.instance);
    converted = std::sscanf(line.ToChar(), "%d %d %d %d", &weight, &type, &group, &instance);
    if (converted >= 1)
      entries[i].weight = static_cast<u32>(weight);
    if (converted >= 2)
      entries[i].key.type = static_cast<u32>(type);
    if (converted >= 3)
      entries[i].key.group = static_cast<u32>(group);
    if (converted >= 4)
      entries[i].key.instance = static_cast<u32>(instance);
    result = converted == 4;
    entries[i].weight *= 10000;
  }
  return result;
}

bool NwVehicleProducerAttribFactory::New(u32, u32 iid, void **out) {
  NwVehicleProducerAttrib *resource = new NwVehicleProducerAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwVehicleProducerAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehicleProducerAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwVehicleProducerAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwVehicleProducerAttrib *>(resource)->SerialWrite(stream) != 0;
}

NwTrainProducerAttrib::NwTrainProducerAttrib() : loaded(0), minimum(0), maximum(0) {
}

NwTrainProducerAttrib::~NwTrainProducerAttrib() {
  if (loaded)
    Shutdown();
}

bool NwTrainProducerAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwTrainProducerAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwTrainProducerAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

// Clear the original loaded flag without destroying train types or changing the interval values.
u8 NwTrainProducerAttrib::Shutdown() {
  if (loaded)
    loaded = 0;
  return 1;
}

// Read the train-production intervals and each type's four resource lists in the original text-record order.
u8 NwTrainProducerAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d %d", &minimum, &maximum);
  minimum = i32(u32(minimum) * kNwTrainIntervalInputScale);
  maximum = i32(u32(maximum) * kNwTrainIntervalInputScale);
  NwStreetLayer::GetTextLine(stream, &line);
  i32 typeCount = 0, listCount = 0, trainValue = 0;
  i32 values[4] = {};
  std::sscanf(line.ToChar(), "%d", &typeCount);
  for (u32 i = 0; i < u32(typeCount); ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    NwTrainType train;
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d", &trainValue);
    train.value = trainValue;
    std::vector<u32> *lists[4] = {&train.values0, &train.values1, &train.values2, &train.values3};
    for (u32 group = 0; group < 4; ++group) {
      NwStreetLayer::GetTextLine(stream, &line);
      std::sscanf(line.ToChar(), "%d", &listCount);
      for (u32 j = 0; j < u32(listCount); ++j) {
        NwStreetLayer::GetTextLine(stream, &line);
        std::sscanf(line.ToChar(), "%d", &values[group]);
        lists[group]->push_back(u32(values[group]));
      }
    }
    types.push_back(train);
  }
  loaded = 1;
  return 1;
}

NwTrainProducerAttribFactory::NwTrainProducerAttribFactory() {
}

bool NwTrainProducerAttribFactory::New(u32, u32 iid, void **out) {
  NwTrainProducerAttrib *resource = new NwTrainProducerAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwTrainProducerAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwTrainProducerAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwTrainProducerAttribFactory::SerialWrite(RsResource *, RsStream *) {
  return false;
}

NwAirTrafficAttrib::NwAirTrafficAttrib() : loaded(0) {
}

NwAirTrafficAttrib::~NwAirTrafficAttrib() {
  if (loaded)
    Shutdown();
}

bool NwAirTrafficAttrib::DebugInstanceTag(ScStringView *out) {
  return RsResource::DebugInstanceTag(out);
}

bool NwAirTrafficAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwAirTrafficAttrib::DebugTypeTag(ScStringView *out) {
  return RsResource::DebugTypeTag(out);
}

// Clear the loaded flag without changing the owned list or scalar field.
u8 NwAirTrafficAttrib::Shutdown() {
  if (loaded)
    loaded = 0;
  return 1;
}

u32 NwAirTrafficAttrib::GetAPlaneResInstId() {
  return planeIds[NwAirTrafficAttribRandom.Bounded(static_cast<u32>(planeIds.size()))];
}

// Read the air-traffic scalar and append the listed plane resource instance IDs in their original order.
u8 NwAirTrafficAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d", &value);
  NwStreetLayer::GetTextLine(stream, &line);
  i32 count = 0, instance = 0;
  std::sscanf(line.ToChar(), "%d", &count);
  for (u32 i = 0; i < u32(count); ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d", &instance);
    planeIds.push_back(u32(instance));
  }
  loaded = 1;
  return 1;
}

NwAirTrafficAttribFactory::NwAirTrafficAttribFactory() {
}

bool NwAirTrafficAttribFactory::New(u32, u32 iid, void **out) {
  NwAirTrafficAttrib *resource = new NwAirTrafficAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwAirTrafficAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwAirTrafficAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwAirTrafficAttribFactory::SerialWrite(RsResource *, RsStream *) {
  return false;
}

bool NwTrafficAttribFactory::New(u32, u32 iid, void **out) {
  NwTrafficAttrib *resource = new NwTrafficAttrib;
  return resource->QueryInterface(iid, out);
}

bool NwTrafficAttribFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<NwTrafficAttrib *>(resource)->SerialRead(stream) != 0;
}

bool NwTrafficAttribFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<NwTrafficAttrib *>(resource)->SerialWrite(stream) != 0;
}

// Initialize each existing people graph before the vehicle graph and preserve the original successful byte.
u8 NwTrafficAttrib::Init() {
  if (peopleGraph)
    peopleGraph->Init();
  if (vehicleGraph)
    vehicleGraph->Init();
  return 1;
}

// Destroy the owned vehicle graph first, followed by the people graph, clearing each field after its destructor.
u8 NwTrafficAttrib::Shutdown() {
  ScDelete(&vehicleGraph);
  ScDelete(&peopleGraph);
  return 1;
}

// Write block corners, the stored flag and both owned graphs using the original binary record order.
u8 NwTrafficAttrib::SerialWrite(RsStream *stream) {
  stream->WriteU8('B');
  stream->WriteU8('I');
  stream->WriteU8('N');
  stream->WriteU8('\r');
  stream->WriteI32(i32(u32(cornerOffsets[0]) << 8));
  stream->WriteI32(i32(u32(cornerOffsets[3]) << 8));
  stream->WriteI32(i32(u32(cornerOffsets[2]) << 8));
  stream->WriteI32(i32(u32(cornerOffsets[1]) << 8));
  stream->WriteU8(flag);
  u8 result = peopleGraph->SerialWrite(stream);
  if (result)
    result = vehicleGraph->SerialWrite(stream);
  return result;
}

// Read text block corners and flag, then construct and read the two graphs, or dispatch the binary record.
u8 NwTrafficAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  std::sscanf(line.ToChar(), "%d %d %d %d", &cornerOffsets[0], &cornerOffsets[3], &cornerOffsets[2],
      &cornerOffsets[1]);
  for (i32 &corner : cornerOffsets)
    corner >>= 8;
  NwStreetLayer::GetTextLine(stream, &line);
  i32 value = 0;
  std::sscanf(line.ToChar(), "%d", &value);
  flag = value != 0;
  NwStreetLayer::GetTextLine(stream, &line);
  peopleGraph = new NwPeopleGraph(this);
  if (!peopleGraph->SerialRead(stream))
    return 0;
  vehicleGraph = new NwVehicleGraph(this);
  return vehicleGraph->SerialRead(stream);
}

// Read binary corners and the flag, then allocate and read the people and vehicle graphs in original order.
u8 NwTrafficAttrib::SerialReadBinary(RsStream *stream) {
  if (!stream->ReadI32(&cornerOffsets[0]))
    return 0;
  cornerOffsets[0] >>= 8;
  if (!stream->ReadI32(&cornerOffsets[3]))
    return 0;
  cornerOffsets[3] >>= 8;
  if (!stream->ReadI32(&cornerOffsets[2]))
    return 0;
  cornerOffsets[2] >>= 8;
  if (!stream->ReadI32(&cornerOffsets[1]))
    return 0;
  cornerOffsets[1] >>= 8;
  // The original uses the high byte of its initialized 32-bit stream-argument slot as the unchecked read output.
  u8 value = u8(u32(uptr(stream)) >> 24);
  stream->ReadU8(&value);
  flag = value != 0;
  peopleGraph = new NwPeopleGraph(this);
  if (!peopleGraph->SerialRead(stream))
    return 0;
  vehicleGraph = new NwVehicleGraph(this);
  return vehicleGraph->SerialRead(stream);
}

NwTrafficBlockObject::NwTrafficBlockObject()
    : attributes(nullptr), lastVehicles(nullptr), peopleCounts(nullptr), references(0), blockedPaths(0),
      entranceBlocked(0) {
}

NwTrafficBlockObject::~NwTrafficBlockObject() {
  Shutdown();
}

// Publish the block for the unknown and traffic-block identities, clearing the output before checking either ID.
bool NwTrafficBlockObject::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwTrafficBlock);
  HANDLE_QUERY_INTERFACE(IID_NwTrafficBlock, NwTrafficBlock);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwTrafficBlockObject::AddRef() {
  return ++references;
}

u32 NwTrafficBlockObject::Release() {
  return ScReleaseImpl(this, &references);
}

// Resolve the real cached block attributes from the resource instance and allocate zeroed vehicle and people arrays.
u8 NwTrafficBlockObject::Init(const RsKey &key) {
  attributes = NwStreetLayer::GetSSTrBlockAttrib(key.instance);
  u32 vehicleCount = attributes->vehicleGraph->pathCount;
  lastVehicles = new NwVehicle *[vehicleCount] {};
  i32 peopleCount = attributes->peopleGraph->pathCount;
  peopleCounts = new i8[peopleCount]{};
  return 1;
}

// Release both path arrays before clearing the borrowed attribute pointer.
u8 NwTrafficBlockObject::Shutdown() {
  if (lastVehicles) {
    delete[] lastVehicles;
    lastVehicles = nullptr;
  }
  if (peopleCounts) {
    delete[] peopleCounts;
    peopleCounts = nullptr;
  }
  if (attributes)
    attributes = nullptr;
  return 1;
}

// Borrow the last vehicle recorded at the signed path index without changing its ownership.
NwVehicle *NwTrafficBlockObject::LastVehicle(i8 path) {
  return lastVehicles[path];
}

// Force entry skips those checks but still asks the previous vehicle to accept its follower.
u8 NwTrafficBlockObject::EnterVehicle(NwVehiclePath *path, NwVehicle *vehicle, i32 distance, bool force) {
  NwVehicleGraphEdge *edge = path->GetEdge();
  i8 index = edge->pathIndex;
  if (force) {
    if (lastVehicles[index] && !lastVehicles[index]->SetFollowingVehicle(vehicle))
      return 0;
  } else {
    if (entranceBlocked)
      return 0;
    if (blockedPaths)
      for (i8 crossing : edge->vehicleCrossings)
        if (blockedPaths & (u32(1) << (u8(crossing) & 31)))
          return 0;
    for (i8 crossing : edge->peopleCrossings)
      if (peopleCounts[crossing] > 0)
        return 0;
    for (i8 crossing : edge->vehicleCrossings)
      if (lastVehicles[crossing])
        return 0;
    if (lastVehicles[index]) {
      i32 length = path->Length();
      if (lastVehicles[index]->DistanceToBack(length, path) < distance)
        return 0;
      lastVehicles[index]->SetFollowingVehicle(vehicle);
    }
  }
  vehicle->SetVehicleAhead(lastVehicles[index]);
  lastVehicles[index] = vehicle;
  return 1;
}

void NwTrafficBlockObject::VehicleLeftPath(i8 path, NwVehicle *vehicle) {
  if (lastVehicles[path] == vehicle)
    lastVehicles[path] = nullptr;
}

void NwTrafficBlockObject::BlockPath(NwVehiclePath *path) {
  u8 index = u8(path->GetEdge()->pathIndex);
  blockedPaths = u16(blockedPaths | (u32(1) << (index & 31)));
}

void NwTrafficBlockObject::UnblockPath(NwVehiclePath *path) {
  u8 index = u8(path->GetEdge()->pathIndex);
  blockedPaths = u16(blockedPaths & ~(u32(1) << (index & 31)));
}

void NwTrafficBlockObject::DisallowVehicleEntrance() {
  entranceBlocked = 1;
}

void NwTrafficBlockObject::AllowVehicleEntrance() {
  entranceBlocked = 0;
}

u8 NwTrafficBlockObject::ContainsVehicles() {
  u32 count = attributes->vehicleGraph->pathCount;
  for (u32 index = 0; index < count; ++index)
    if (lastVehicles[index])
      return 1;
  return 0;
}

NwTrafficFactoryObject::NwTrafficFactoryObject() : references(0) {
}

bool NwTrafficFactoryObject::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwTrafficBlockFactory);
  HANDLE_QUERY_INTERFACE(IID_NwTrafficBlockFactory, NwTrafficBlockFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the director after increasing the factory reference count and return the live count afterward.
u32 NwTrafficFactoryObject::AddRef() {
  ++references;
  NwGetStreetDirector()->AddRef();
  return references;
}

// Release the module hold before decrementing a nonzero factory reference, then delete at zero.
u32 NwTrafficFactoryObject::Release() {
  if (references) {
    NwGetStreetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

u8 NwTrafficFactoryObject::CreateStrtSimTrBlock(const RsKey &key, void **out) {
  NwTrafficBlockObject *block = new NwTrafficBlockObject;
  if (block->Init(key))
    return block->QueryInterface(IID_NwTrafficBlock, out);
  delete block;
  *out = nullptr;
  return 0;
}

// Construct the zero-reference traffic-block factory and return its actual primary interface.
ScUnknown *NwCreateTrafficFactory() {
  return static_cast<NwTrafficBlockFactory *>(new NwTrafficFactoryObject);
}
