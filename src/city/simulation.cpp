#include "city/simulation.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <cassert>
#include <new>

#include "city/api.h"
#include "city/ct_date.h"
#include "city/module.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"

// Paused simulation polls the city pause depth at the original thread-sleep interval.
enum CtPauseWaitTiming : i32 {
  kCtPausePollSleepInterval = 0xf4240,
};

// Simulator pointer and deferred-record storage preserve their original independent maximum entry counts.
enum CtSimulatorStorageLimit : u32 {
  kCtPendingAgentMaximumCount = 0x1fffffff,
  kCtManipulatorMaximumCount = 0x3fffffff,
};

// Construct the name after zeroing references, then clear links before assigning an independent default-name temporary.
// Retire that temporary before clearing the payload word and raw expansion byte.
ScNamedNode::ScNamedNode() : references(0) {
  parentNode = siblingNode = previousSiblingNode = firstChildNode = nullptr;
  {
    ScString initial("*NONAME*");
    nodeName = initial;
  }
  nodeData = 0;
  expansionState = 0;
}

ScNamedNode::~ScNamedNode() {
}

// Publish the controlling node identity and retain through the live virtual method for either supported IID. Refusal
// clears the output.
bool ScNamedNode::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScTreeNode);
  HANDLE_QUERY_INTERFACE(IID_ScTreeNode, ScTreeNode);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScNamedNode::AddRef() {
  return ++references;
}

u32 ScNamedNode::Release() {
  return ScReleaseImpl(this, &references);
}

// Return the owned name's borrowed string interface without copying its characters or retaining it.
ScStringView *ScNamedNode::GetNodeText() {
  return &nodeName;
}

void ScNamedNode::SetNodeText(ScStringView *text) {
  ScString copy(*text);
  nodeName = copy;
}

// Borrow the node's mutable unsigned payload word without copying it or changing tree ownership.
// The installed getter returns its field address, matching the Linux GetNodeData accessor.
u32 *ScNamedNode::GetNodeData() {
  return &nodeData;
}

void ScNamedNode::SetNodeData(u32 data) {
  nodeData = data;
}

bool ScNamedNode::HasChildren() {
  return firstChildNode != nullptr;
}

// Return the borrowed parent identity stored at node+8; no hold or adjustment is introduced.
// The original getter is folded with unrelated field-load methods.
ScTreeNode *ScNamedNode::GetParent() {
  return parentNode;
}

// Return the borrowed next sibling stored at node+12 without querying its provider.
ScTreeNode *ScNamedNode::GetNextSibling() {
  return siblingNode;
}

// Return the preceding sibling stored at node+16 without changing either link.
// Original1000E5F7 is a folded field load; its other receiver aliases remain distinct.
ScTreeNode *ScNamedNode::GetPrevSibling() {
  return previousSiblingNode;
}

// Return the borrowed first child stored at node+20 without retaining it.
// Original1000115A also serves unrelated classes' field loads.
ScTreeNode *ScNamedNode::GetFirstChild() {
  return firstChildNode;
}

// Follow live parent queries to the root, querying each nonnull link again before advancing.
ScTreeNode *ScNamedNode::GetRoot() {
  ScTreeNode *node = this;
  while (node->GetParent())
    node = node->GetParent();
  return node;
}

// Follow preceding siblings to the first node, preserving the original repeated virtual queries.
ScTreeNode *ScNamedNode::GetFirstSibling() {
  ScTreeNode *node = this;
  while (node->GetPrevSibling())
    node = node->GetPrevSibling();
  return node;
}

// Follow next siblings to the final node, observing the link again before every advance.
ScTreeNode *ScNamedNode::GetLastSibling() {
  ScTreeNode *node = this;
  while (node->GetNextSibling())
    node = node->GetNextSibling();
  return node;
}

// Query the first child once and dispatch its last-sibling operation when a child exists.
ScTreeNode *ScNamedNode::GetLastChild() {
  ScTreeNode *node = GetFirstChild();
  return node ? node->GetLastSibling() : nullptr;
}

// Publish the child's parent before validating its insertion position, then relink through the live virtual node methods.
// Position0 prepends; pointer sentinels1/2 append; any other position must occur in this receiver's sibling chain.
bool ScNamedNode::AddChild(ScTreeNode *child, ScTreeNode *position) {
  if (reinterpret_cast<uptr>(position) == 2)
    position = reinterpret_cast<ScTreeNode *>(uptr(1));
  child->SetParent(this);
  if (!firstChildNode) {
    firstChildNode = child;
    child->SetNextSibling(nullptr);
    child->SetPrevSibling(nullptr);
  } else if (reinterpret_cast<uptr>(position) == 1) {
    ScTreeNode *last = firstChildNode;
    while (last->GetNextSibling())
      last = last->GetNextSibling();
    last->SetNextSibling(child);
    child->SetPrevSibling(last);
    child->SetNextSibling(nullptr);
  } else if (!position) {
    child->SetPrevSibling(nullptr);
    child->SetNextSibling(GetFirstChild());
    firstChildNode->SetPrevSibling(child);
    firstChildNode = child;
  } else {
    ScTreeNode *node = GetFirstChild();
    while (node && node != position)
      node = node->GetNextSibling();
    if (!node)
      return false;
    child->SetNextSibling(position->GetNextSibling());
    child->SetPrevSibling(position);
    if (position->GetNextSibling())
      position->GetNextSibling()->SetPrevSibling(child);
    position->SetNextSibling(child);
  }
  return true;
}

// Relink live neighboring nodes and update the parent's first child before clearing only this node's parent field.
// The original void method leaves the detached node's sibling fields untouched.
void ScNamedNode::DetachFromTree() {
  if (GetNextSibling())
    GetNextSibling()->SetPrevSibling(GetPrevSibling());
  if (GetPrevSibling())
    GetPrevSibling()->SetNextSibling(GetNextSibling());
  if (parentNode->GetFirstChild() == this) {
    ScTreeNode *next = GetNextSibling();
    parentNode->SetFirstChild(next);
  }
  parentNode = nullptr;
}

void ScNamedNode::DeleteChildren() {
  while (ScTreeNode *node = GetBottomLeftChild()) {
    node->DetachFromTree();
    ::operator delete(node);
  }
}

// Detach and free each immediate first child using the node's live virtual links.
// Descendant ownership and destructor dispatch are intentionally absent from the original operation.
void ScNamedNode::DeleteAllChildren() {
  while (ScTreeNode *node = GetFirstChild()) {
    node->DetachFromTree();
    ::operator delete(node);
  }
}

// Begin with the stored first child, then descend through repeated live first-child queries.
// An empty receiver returns null rather than the receiver itself.
ScTreeNode *ScNamedNode::GetBottomLeftChild() {
  ScTreeNode *node = firstChildNode;
  if (node)
    while (node->GetFirstChild())
      node = node->GetFirstChild();
  return node;
}

// Ask the last child for its deepest-last-child result; an empty receiver returns its own identity.
// This dispatch intentionally differs from simply following the last-child link at every level.
ScTreeNode *ScNamedNode::GetBottomRightChild() {
  ScTreeNode *node = GetLastChild();
  return node ? node->GetBottomRightChild() : this;
}

bool ScNamedNode::IsExpanded() {
  return expansionState;
}

void ScNamedNode::SetExpanded(bool state) {
  expansionState = state;
}

// Descend only when the live expansion byte is nonzero; otherwise seek a sibling while ascending.
// Successful child, sibling and parent tests are reread before following their selected link.
ScTreeNode *ScNamedNode::GetNextDrawableNode() {
  ScTreeNode *node = this;
  if (node->IsExpanded() && node->GetFirstChild())
    return node->GetFirstChild();
  for (;;) {
    if (node->GetNextSibling())
      return node->GetNextSibling();
    if (!node->GetParent())
      break;
    node = node->GetParent();
  }
  return nullptr;
}

// Descend to the first child or find the next sibling while ascending through parents as needed.
// Each successful link test is reread before following it, preserving live virtual query ordering.
ScTreeNode *ScNamedNode::GetNextInDisplayOrder() {
  ScTreeNode *node = this;
  if (node->GetFirstChild())
    return node->GetFirstChild();
  for (;;) {
    if (node->GetNextSibling())
      return node->GetNextSibling();
    if (!node->GetParent())
      break;
    node = node->GetParent();
  }
  return nullptr;
}

// Query the preceding sibling's deepest-last-child operation, falling back to a fresh sibling query on null.
// With no preceding sibling, return the live parent directly.
ScTreeNode *ScNamedNode::GetPrevInDisplayOrder() {
  if (!GetPrevSibling())
    return GetParent();
  ScTreeNode *node = GetPrevSibling()->GetBottomRightChild();
  return node ? node : GetPrevSibling();
}

// Test whether this receiver occurs strictly above the supplied node in its live parent chain.
// Repeated queries remain separate, and a null supplied node produces false.
bool ScNamedNode::IsDescendant(ScTreeNode *node) {
  if (node)
    while (node->GetParent()) {
      node = node->GetParent();
      if (node == this)
        return true;
    }
  return false;
}

bool ScNamedNode::IsAncestor(ScTreeNode *candidate) {
  for (ScTreeNode *node = GetParent(); node; node = node->GetParent())
    if (node == candidate)
      return true;
  return false;
}

// Search from the first sibling through next-sibling links, including this receiver in the membership test.
bool ScNamedNode::IsSibling(ScTreeNode *candidate) {
  for (ScTreeNode *node = GetFirstSibling(); node; node = node->GetNextSibling())
    if (node == candidate)
      return true;
  return false;
}

bool ScNamedNode::OnNextSearchNode(u32 data, ScTreeNode *node) {
  return data == *node->GetNodeData();
}

// Begin at the deepest first child, then test live siblings and ascend until this receiver's boundary is reached.
// Preserve the original repeated link queries and virtual predicate; do not descend into later sibling subtrees.
ScTreeNode *ScNamedNode::Search(u32 data) {
  ScTreeNode *node = GetBottomLeftChild();
  while (node) {
    if (OnNextSearchNode(data, node))
      return node;
    if (node->GetNextSibling())
      node = node->GetNextSibling();
    else {
      if (!node->GetParent() || node->GetParent() == this)
        return nullptr;
      node = node->GetParent();
    }
  }
  return nullptr;
}

// Count the depth-first suffix beginning at the first child, preserving traversal beyond this subtree.
// The original interface returns an unsigned count.
u32 ScNamedNode::GetNumDescendents() {
  u32 count = 0;
  for (ScTreeNode *node = GetFirstChild(); node; node = node->GetNextInDisplayOrder())
    ++count;
  return count;
}

// Count parent hops from the first live parent through the original traversal loop.
// The root has unsigned distance zero.
u32 ScNamedNode::GetDistanceFromRoot() {
  ScTreeNode *node = GetParent();
  u32 depth = 0;
  while (node) {
    node = node->GetParent();
    ++depth;
  }
  return depth;
}

void ScNamedNode::SetParent(ScTreeNode *node) {
  parentNode = node;
}

void ScNamedNode::SetNextSibling(ScTreeNode *node) {
  siblingNode = node;
}

void ScNamedNode::SetPrevSibling(ScTreeNode *node) {
  previousSiblingNode = node;
}

void ScNamedNode::SetFirstChild(ScTreeNode *node) {
  firstChildNode = node;
}

CtManipulatorStorage::CtManipulatorStorage() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

CtManipulatorStorage::~CtManipulatorStorage() {
  delete[] begin;
}

// Grow the dedicated pointer storage only when full and append without changing the manipulator's hold.
void CtManipulatorStorage::Append(CtLayerManipulator *manipulator) {
  if (end == storageEnd) {
    u32 count = begin ? (u32)(end - begin) : 0;
    if (count >= kCtManipulatorMaximumCount)
      ScFatal(SC_ERROR_NOMEM, "Agent manipulator storage is too long");
    u32 capacity = count ? count * 2 : 1;
    if (capacity > kCtManipulatorMaximumCount)
      capacity = kCtManipulatorMaximumCount;
    CtLayerManipulator **values = new CtLayerManipulator *[capacity];
    for (u32 i = 0; i < count; ++i)
      values[i] = begin[i];
    delete[] begin;
    begin = values;
    end = values + count;
    storageEnd = values + capacity;
  }
  *end++ = manipulator;
}

// Shift only a positive live suffix, then shorten the vector without releasing the erased interface.
void CtManipulatorStorage::Erase(CtLayerManipulator **position) {
  isize count = end - (position + 1);
  for (isize i = 0; i < count; ++i)
    position[i] = position[i + 1];
  --end;
}

void CtManipulatorStorage::Clear() {
  end = begin;
}

// Construct the original six phase vectors and their lookup pointers after the secondary node base.
CtAgentTypeImpl::CtAgentTypeImpl() {
  name = {0, 0, 0};
  parentType = firstChildType = siblingType = nullptr;
  hasManipulators = false;
  phases[3] = &storage[3];
  phases[4] = &storage[4];
  phases[2] = &storage[2];
  phases[5] = &storage[5];
  phases[0] = &storage[0];
  phases[1] = &storage[1];
}

CtAgentTypeImpl::~CtAgentTypeImpl() {
  if (hasManipulators)
    ReleaseAllManipulators();
}

bool CtAgentTypeImpl::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtAgentType, CtAgentType);
  return ScNamedNode::QueryInterface(iid, out);
}

u32 CtAgentTypeImpl::AddRef() {
  return ScNamedNode::AddRef();
}

u32 CtAgentTypeImpl::Release() {
  return ScNamedNode::Release();
}

// Return the borrowed secondary tree-node interface of this complete type object.
ScTreeNode *CtAgentTypeImpl::AsIGZTreeNode() {
  return static_cast<ScTreeNode *>(this);
}

u32 CtAgentTypeImpl::Id() const {
  return typeId;
}

bool CtAgentTypeImpl::SetId(u32 id) {
  typeId = id;
  return true;
}

// Append to the selected phase before retaining the manipulator, then mark the type active.
bool CtAgentTypeImpl::AttachManipulator(CtLayerManipulator *manipulator, i32 flags) {
  phases[flags]->Append(manipulator);
  manipulator->AddRef();
  hasManipulators = true;
  return true;
}

// Erase matching pointers across the captured original range without releasing their retained interfaces.
bool CtAgentTypeImpl::ReleaseManipulator(CtLayerManipulator *manipulator, i32 flags) {
  CtManipulatorStorage *selected = phases[flags];
  CtLayerManipulator **position = selected->begin, **last = selected->end;
  bool removed = false;
  while (position != last) {
    if (*position == manipulator) {
      selected->Erase(position);
      removed = true;
    }
    ++position;
  }
  if (!removed)
    return false;
  hasManipulators = selected->begin != selected->end;
  return true;
}

// Clear the active flag before releasing each phase's captured range, then clear that phase's live range.
// A refused inactive cleanup performs no releases even if another phase still contains retained pointers.
void CtAgentTypeImpl::ReleaseAllManipulators() {
  if (hasManipulators) {
    hasManipulators = false;
    for (u32 i = 0; i < 6; ++i) {
      CtLayerManipulator **position = phases[i]->begin, **last = phases[i]->end;
      while (position != last) {
        (*position)->Release();
        ++position;
      }
      phases[i]->Clear();
    }
  }
}

// Dispatch the selected phase's captured manipulators, requesting an immediate simulator pause when the live city is paused.
// Query each manipulator's locking flag separately before and after Apply, retaining the original changing-service lookups.
bool CtAgentTypeImpl::Simulate(CtAgent *agent, i32 elapsed) {
  CtLayerManipulator **position = phases[elapsed]->begin, **last = phases[elapsed]->end;
  while (position != last) {
    if (CtGetCity()->IsPaused())
      CtGetCity()->CitySimulator()->DoSimImmediatePause();
    if (!(*position)->IsLockingCity())
      CtGetCity()->Lock();
    (*position)->Apply(agent);
    if (!(*position)->IsLockingCity())
      CtGetCity()->Unlock();
    ++position;
  }
  return true;
}

// Return the first nonempty phase index, or the original sentinel six when all phase ranges are empty.
i32 CtAgentTypeImpl::MinimumExecutionInterval() {
  for (i32 i = 0; i < 6; ++i)
    if (phases[i]->begin != phases[i]->end)
      return i;
  return 6;
}

// Return the borrowed primary parent identity; the optimized source getter10008AEF is shared with other classes.
CtAgentType *CtAgentTypeImpl::Parent() const {
  return parentType;
}

// Return the borrowed primary first-child identity, independent of the secondary node's link.
CtAgentType *CtAgentTypeImpl::FirstChild() const {
  return firstChildType;
}

// Return the borrowed primary sibling identity without changing its hold.
CtAgentType *CtAgentTypeImpl::Sibling() const {
  return siblingType;
}

void CtAgentTypeImpl::GetName(RsKey *out) {
  *out = name;
}

bool CtAgentTypeImpl::SetName(const RsKey &key) {
  name = key;
  return true;
}

// Publish the primary parent before obtaining and installing its secondary node identity.
// A null parent changes only the primary field and deliberately leaves the secondary link untouched.
bool CtAgentTypeImpl::SetParent(CtAgentType *parent) {
  parentType = parent;
  if (parent)
    static_cast<ScTreeNode *>(this)->SetParent(parent->AsIGZTreeNode());
  return true;
}

// Store the primary first child before forwarding a nonnull child's node view through the live secondary interface.
// Clearing the primary identity does not clear the original secondary first-child link.
bool CtAgentTypeImpl::SetFirstChild(CtAgentType *child) {
  firstChildType = child;
  if (child)
    static_cast<ScTreeNode *>(this)->SetFirstChild(child->AsIGZTreeNode());
  return true;
}

// Replace the borrowed primary sibling, updating the secondary sibling link only for a nonnull replacement.
bool CtAgentTypeImpl::SetSibling(CtAgentType *sibling) {
  siblingType = sibling;
  if (sibling)
    static_cast<ScTreeNode *>(this)->SetNextSibling(sibling->AsIGZTreeNode());
  return true;
}

bool CtAgentTypeImpl::DebugInstanceTag(ScStringView *) {
  return false;
}

void CtAgentTypeImpl::DebugSetInstanceTag(ScStringView *) {
}

bool CtAgentTypeImpl::DebugClassTag(ScStringView *) {
  return false;
}

bool CtAgentTypeImpl::DebugTypeTag(ScStringView *) {
  return false;
}

CtAgentImpl::CtAgentImpl() : type(nullptr), references(0) {
}

CtAgentImpl::~CtAgentImpl() {
  ScRelease(&type);
}

// Clear the output before testing the two original identities, then retain a successful primary through its live AddRef.
bool CtAgentImpl::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtAgentImpl);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgentImpl);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Publish the new agent reference before retaining the module, then return the current word after that callback.
u32 CtAgentImpl::AddRef() {
  ++references;
  CtGetDirector()->AddRef();
  return references;
}

// Drop an existing reference before removing its module hold; a zero count also deletes without module removal.
// Reread after the director callback so nested ownership changes determine whether this receiver survives.
u32 CtAgentImpl::Release() {
  if (references) {
    --references;
    CtGetDirector()->RemoveRef();
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool CtAgentImpl::Simulate(i32 elapsed) {
  return type->Simulate(this, elapsed);
}

i32 CtAgentImpl::MinimumExecutionInterval() {
  return type ? type->MinimumExecutionInterval() : 6;
}

bool CtAgentImpl::IsAgentLocking() {
  return true;
}

// Request the type's identifier for its original side effects, but preserve this agent method's literal zero result.
u32 CtAgentImpl::TypeId() const {
  if (type)
    type->Id();
  return 0;
}

// Return the borrowed type identity without adding another reference.
CtAgentType *CtAgentImpl::AgentType() {
  return type;
}

// Release the previous type before publishing and retaining the required nonnull replacement.
// The original does not clear the old field during Release and always returns true after the new AddRef.
bool CtAgentImpl::SetAgentType(CtAgentType *source) {
  if (type)
    type->Release();
  type = ScAddRefNotNull(source);
  return true;
}

bool CtAgentImpl::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtAgentImpl::DebugClassTag(ScStringView *) {
  return false;
}

bool CtAgentImpl::DebugTypeTag(ScStringView *) {
  return false;
}

// Construct the default agent without acquiring an initial hold; its caller performs the requested interface query.
// This is the registered constructor phase at SIMCITY1000258D, with private nominal return spelling unresolved.
ScUnknown *CtCreateAgent() {
  return new CtAgentImpl;
}

// Publish the new agent type through its secondary tree-node identity, preserving the controlling Unknown interface.
// SIMCITY1000254F returns complete receiver+4; it does not expose the primary agent-type view or add a reference.
ScUnknown *CtCreateAgentType() {
  return static_cast<ScTreeNode *>(new CtAgentTypeImpl);
}

i32 CtCalendarLeapYear(u32 year) {
  return (!(year & 3) && year % 100 != 0) || year % 400 == 0;
}

i32 CtCalendarValid(u32 month, u32 day, u32 year) {
  static const u8 lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (!day || !month || month > 12)
    return 0;
  u32 limit = lengths[month - 1];
  if (CtCalendarLeapYear(year) && month == 2)
    ++limit;
  return day <= limit;
}

// Encode a valid Gregorian date as a Julian-day word, returning zero for an invalid month/day.
// Products wrap to 32 bits before the original unsigned shifts and divisions.
u32 CtCalendarEncode(u32 month, u32 day, u32 year) {
  if (!CtCalendarValid(month, day, year))
    return 0;
  u32 shiftedMonth;
  if (month <= 2) {
    shiftedMonth = month + 9;
    --year;
  } else
    shiftedMonth = month - 3;
  u32 century = year / 100;
  return ((kCtGregorianFourCenturyDays * century) >> 2) + ((1461u * year - kCtJulianFourCenturyDays * century) >> 2) +
      day + (153u * shiftedMonth + 2) / 5 + kCtMarchCalendarSerialOffset;
}

// Construct today's local date through the original 32-bit CRT time boundary.
CtDate::CtDate() {
  tm local;
  ScReadLocalDate(&local);
  serial = CtCalendarEncode((u32)local.tm_mon + 1, (u32)local.tm_mday, (u32)local.tm_year + 1900);
}

CtDate::CtDate(u32 day, u32 year) : serial(day + (year ? CtCalendarEncode(12, 31, year - 1) : kCtTimeEpochDay)) {
}

CtDate::CtDate(u32 month, u32 day, u32 year) : serial(CtCalendarEncode(month, day, year)) {
}

CtDate::CtDate(const CtDate &source) : serial(source.serial) {
}

// Decode through the three original outputs. Ordered reads and writes preserve defined output-alias cases.
void CtDate::Mdy(u32 *month, u32 *day, u32 *year) const {
  u32 fourDays = 4u * (serial - kCtMarchCalendarSerialOffset), century = (fourDays - 1) / kCtGregorianFourCenturyDays;
  *year = century;
  u32 withinCentury = 4u * ((fourDays - kCtGregorianFourCenturyDays * century - 1) >> 2);
  u32 localYear = (withinCentury + 3) / 1461u;
  u32 fiveDays = 5u * ((withinCentury - 1461u * localYear + 7) >> 2);
  u32 localMonth = (fiveDays - 3) / 153u;
  *month = localMonth;
  *day = (fiveDays - 153u * localMonth + 2) / 5;
  *year = localYear + 100u * *year;
  if (*month >= 10) {
    *month -= 9;
    ++*year;
  } else
    *month += 3;
}

// Extract the day-of-month component through the same decoder used by formatted dates.
u32 CtDate::DayOfMonth() const {
  u32 month, day, year;
  Mdy(&month, &day, &year);
  return day;
}

u32 CtDate::DayOfYear() const {
  return serial - CtCalendarEncode(12, 31, Year() - 1);
}

// Extract the month component without normalizing out-of-range serial words.
u32 CtDate::Month() const {
  u32 month, day, year;
  Mdy(&month, &day, &year);
  return month;
}

// Extract the unsigned Gregorian year using the original decoder.
u32 CtDate::Year() const {
  u32 month, day, year;
  Mdy(&month, &day, &year);
  return year;
}

u32 CtDate::DayOfWeek() const {
  return ((serial + 1) % 7 + 6) % 7 + 1;
}

// Return the nearest requested weekday on or before this date. The compiler owns the aggregate return storage.
CtDate CtDate::OnOrBefore(u32 weekday) const {
  u32 wanted = weekday - 1, current = DayOfWeek() - 1;
  u32 distance = wanted <= current ? current - wanted : 7u - wanted + current;
  CtDate result(*this);
  result.serial -= distance;
  return result;
}

void CtDate::NameOfDayString(ScString *, u32) const {
}

void CtDate::NameOfMonthString(ScString *, u32) const {
}

// The original performs year, day/month lookups separately; keep that order before assigning the string.
void CtDate::DateString(ScString *out, u32 format) const {
  u32 year = Year() % 100, first, second, third;
  const char *pattern = "%2u/%2u/%2u";
  if (format == 9 || format == 15) {
    third = DayOfMonth();
    second = Month();
    first = year;
    if (format == 9)
      pattern = "%2u.%2u.%2u";
  } else if (format == 0 || format == 1 || format == UINT32_MAX) {
    second = DayOfMonth();
    first = Month();
    third = year;
  } else {
    second = Month();
    first = DayOfMonth();
    third = year;
    if (format == 3 || format == 4 || format == 10 || format == 11)
      pattern = "%2u.%2u.%2u";
    else if (format == 7 || format == 8 || format == 14)
      pattern = "%2u-%2u-%2u";
  }
  char buffer[64];
  int length = snprintf(buffer, sizeof(buffer), pattern, first, second, third);
  out->FromChar(buffer, (u32)length);
}

// Read the current local calendar time and convert it through the game's date and timestamp constructors.
// The native calendar query must supply a valid shared CRT result.
CtTime::CtTime() {
  tm local;
  ScReadLocalTime(&local);
  CtDate date(static_cast<u32>(local.tm_mon + 1), static_cast<u32>(local.tm_mday),
      static_cast<u32>(local.tm_year + 1900));
  CtTime value(date, static_cast<u32>(local.tm_hour), static_cast<u32>(local.tm_min), static_cast<u32>(local.tm_sec));
  timestamp = value.timestamp;
}

CtTime::CtTime(u32 value) : timestamp(value) {
}

// Find the original US daylight-saving start, including the 1974 and 1975 exceptions. Return its timestamp value.
CtTime CtTime::DstStart(u32 year) {
  CtDate april(4, 8, year);
  CtDate start = year == 1974 ? CtDate(1, 6, 1974) : year == 1975 ? CtDate(2, 23, 1975) : april.OnOrBefore(7);
  return CtTime(86400u * (start.serial - kCtTimeEpochDay) + ScTimezoneSeconds() + 7200u);
}

// Find the last Sunday of October using the original date helper. The boundary includes the CRT timezone offset.
CtTime CtTime::DstEnd(u32 year) {
  CtDate october(10, 31, year);
  october.OnOrBefore(7);
  return CtTime(86400u * (october.OnOrBefore(7).serial - kCtTimeEpochDay) + ScTimezoneSeconds() + 3600u);
}

// Test the timestamp against the original US seasonal boundaries when the CRT daylight flag is enabled.
i32 CtTime::IsDst() const {
  if (!ScDaylightEnabled())
    return 0;
  u32 year = CtDate(timestamp / 86400u, 0).Year();
  return timestamp >= DstStart(year).timestamp && timestamp < DstEnd(year).timestamp;
}

// Convert a local date/time to the game's timestamp word. Preserve skipped spring hours and repeated autumn hours.
CtTime::CtTime(const CtDate &date, u32 hour, u32 minute, u32 second) {
  if (!date.serial) {
    timestamp = 0;
    return;
  }
  timestamp = 60u * (minute + 60u * (hour + 24u * date.serial)) - 86400u * kCtTimeEpochDay + second - 3600u;
  if (timestamp)
    timestamp += ScTimezoneSeconds();
  if (!IsDst()) {
    timestamp += 3600u;
    if (IsDst())
      timestamp = 0;
    return;
  }
  timestamp += 3600u;
  if (IsDst())
    timestamp -= 3600u;
}

// Decode the local hour after the original wrapped timezone subtraction and seasonal adjustment.
u32 CtTime::Hour() const {
  CtTime local(timestamp - ScTimezoneSeconds());
  if (local.IsDst())
    local.timestamp += 3600u;
  return local.timestamp % 86400u / 3600u;
}

// Decode the local minute using the same seasonal test as the hour accessor.
u32 CtTime::Minute() const {
  CtTime local(timestamp - ScTimezoneSeconds());
  if (local.IsDst())
    local.timestamp += 3600u;
  return local.timestamp % 3600u / 60u;
}

CtDate::CtDate(const CtDateInterface &source) : serial(source.DayNumber()) {
}

CtDate CtDate::Maximum(const CtDate &other) const {
  return CtDate(other.serial > serial ? other : *this);
}

CtDate CtDate::Minimum(const CtDate &other) const {
  return CtDate(other.serial < serial ? other : *this);
}

// Compare complete unsigned serial words after reading the receiver before the input date.
// Publish the original signed three-way result without retaining either date or querying an interface.
i32 CtDate::CompareTo(const CtDate &other) const {
  u32 value = serial, compared = other.serial;
  if (compared > value)
    return -1;
  return compared < value;
}

// Return the requested month's one-based ordinal start and apply the receiver year's leap adjustment after February.
// Invalid month selectors return zero before any year conversion, matching the original helper's short circuit.
u32 CtDate::FirstDayOfMonth(u32 month) const {
  static const u16 first_days[] = {1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};
  if (!month || month > 12)
    return 0;
  u32 day = first_days[month - 1];
  if (month > 2 && CtCalendarLeapYear(CtDate::Year()))
    ++day;
  return day;
}

void CtDate::Set(u32 day, u32 year) {
  serial = day + (year ? CtCalendarEncode(12, 31, year - 1) : kCtTimeEpochDay);
}

void CtDate::Set(u32 month, u32 day, u32 year) {
  serial = CtCalendarEncode(month, day, year);
}

// Convert the timestamp through the original timezone and daylight boundaries. Zero timestamps retain the invalid date.
void CtDate::Set(const CtTime &time) {
  u32 value = time.timestamp;
  if (!value) {
    serial = 0;
    return;
  }
  value -= ScTimezoneSeconds();
  if (CtTime(value).IsDst())
    value += 3600u;
  serial = value / 86400u + kCtTimeEpochDay;
}

void CtDate::Set(u32 serialValue) {
  serial = serialValue;
}

// Scan all twelve original three-letter month names without stopping at a match. Unmatched text selects January.
void CtDate::Set(const char *month, u32 day, u32 year) {
  static const char *const names[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  u32 selected = 1;
  for (u32 i = 0; i < 12; ++i)
    if (!ScCompareNoCase(names[i], month, 3))
      selected = i + 1;
  serial = CtCalendarEncode(selected, day, year);
}

CtDate::CtDate(u32 value) : serial(value) {
}

CtDate::CtDate(const CtTime &time) {
  Set(time);
}

CtDate::CtDate(const char *month, u32 day, u32 year) {
  Set(month, day, year);
}

// Test the stored unsigned date inclusively between either ordering of the two endpoints. Retail cRZDate
// declaration3A6F.
i32 CtDate::Between(const CtDate &first, const CtDate &second) const {
  if (first.serial <= second.serial)
    return serial >= first.serial && serial <= second.serial;
  return serial >= second.serial && serial <= first.serial;
}

u32 CtDate::Hash() const {
  return serial;
}

i32 CtDate::IsValid() const {
  return serial != 0;
}

i32 CtDate::Leap() const {
  return CtCalendarLeapYear(Year());
}

u32 CtDate::YearLastTwoDigits() const {
  return Year() % 100u;
}

u32 CtDate::DayNumber() const {
  return serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 08393150. A standalone Unlimited PC entry is not established.
i32 CtDate::operator<(const CtDate &other) const {
  return serial < other.serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 08393168. A standalone Unlimited PC entry is not established.
i32 CtDate::operator<=(const CtDate &other) const {
  return serial <= other.serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 08393180. A standalone Unlimited PC entry is not established.
i32 CtDate::operator>(const CtDate &other) const {
  return serial > other.serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 08393198. A standalone Unlimited PC entry is not established.
i32 CtDate::operator>=(const CtDate &other) const {
  return serial >= other.serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 083931B0. A standalone Unlimited PC entry is not established.
i32 CtDate::operator==(const CtDate &other) const {
  return serial == other.serial;
}

// Compare the two unsigned Julian-day words directly.
// Retail cRZDate declaration3A6F; Linux 083931C8. A standalone Unlimited PC entry is not established.
i32 CtDate::operator!=(const CtDate &other) const {
  return serial != other.serial;
}

// Subtract the input date word with unsigned wraparound.
// Retail cRZDate declaration3A6F; Linux 083931E0. A standalone Unlimited PC entry is not established.
u32 CtDate::operator-(const CtDate &other) const {
  return serial - other.serial;
}

// Advance one day with unsigned wraparound and return this borrowed value receiver.
// Retail cRZDate declaration3A6F; Linux 083931F4. A standalone Unlimited PC entry is not established.
CtDate *CtDate::operator++() {
  ++serial;
  return this;
}

// Copy the prior value before advancing this date. Return a distinct value with its own formatting identity.
// Retail cRZDate declaration3A6F; Linux 08393210. A standalone Unlimited PC entry is not established.
CtDate CtDate::operator++(int) {
  CtDate previous(*this);
  ++serial;
  return previous;
}

// Retreat one day with unsigned wraparound and return this borrowed value receiver.
// Retail cRZDate declaration3A6F; Linux 08393200. A standalone Unlimited PC entry is not established.
CtDate *CtDate::operator--() {
  --serial;
  return this;
}

// Copy the prior value before retreating this date. Return the prior date as a distinct value.
// Retail cRZDate declaration3A6F; Linux 0839325C. A standalone Unlimited PC entry is not established.
CtDate CtDate::operator--(int) {
  CtDate previous(*this);
  --serial;
  return previous;
}

// Add the signed displacement with unsigned wraparound and return this borrowed receiver.
// Retail cRZDate declaration3A6F; Linux 083932A4. A standalone Unlimited PC entry is not established.
CtDate *CtDate::operator+=(i32 days) {
  serial += static_cast<u32>(days);
  return this;
}

// Subtract the signed displacement with unsigned wraparound and return this borrowed receiver.
// Retail cRZDate declaration3A6F; Linux 083932B8. A standalone Unlimited PC entry is not established.
CtDate *CtDate::operator-=(i32 days) {
  serial -= static_cast<u32>(days);
  return this;
}

u32 CtDate::FirstDayOfMonth() const {
  return FirstDayOfMonth(Month());
}

u32 CtDate::DaysInYear(u32 year) {
  return CtCalendarLeapYear(year) ? 366u : 365u;
}

i32 CtDate::AssertWeekDayNumber(u32 weekday) {
  return weekday - 1u <= 6u;
}

i32 CtDate::AssertIndexOfMonth(u32 month) {
  return month - 1u <= 11u;
}

// Link a fully populated callback record at the tail, preserving callback insertion order.
static void CtAppendSchedule(CtScheduleNode *head, CtScheduleNode *node) {
  node->next = head;
  node->previous = head->previous;
  head->previous->next = node;
  head->previous = node;
}

// Unlink and release a callback node after its necessary next pointer has been captured by the caller.
static void CtEraseSchedule(CtScheduleNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

static void CtClearSchedule(CtScheduleNode *head) {
  while (head->next != head)
    CtEraseSchedule(head->next);
}

CtScheduleList::CtScheduleList() : head(new CtScheduleNode) {
  head->next = head;
  head->previous = head;
}

CtScheduleList::~CtScheduleList() {
  CtClearSchedule(head);
  delete head;
}

// Construct the original queue, elapsed timer, worker and lock in that order, then set the calling thread's priority.
CtScheduler::CtScheduler() : elapsed(ScStopwatch::Milliseconds, 0), nextId{1}, stop(0), running(0) {
  u32 id;
  worker.GetThreadId(&id);
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  ScThread::SetCurrentPriority(2);
}

// Request worker termination, wake it, and retain the original one-second running-flag wait before member teardown.
CtScheduler::~CtScheduler() {
  u32 id;
  worker.GetThreadId(&id);
  stop = 1;
  if (worker.IsRunning()) {
    ScStopwatch waiting(ScStopwatch::Milliseconds, 0);
    worker.Resume();
    worker.Wake();
    waiting.Restart();
    while (waiting.GetElapsedTime() < 1000 && running)
      ;
  }
}

// Consume an ID before locking, append a deadline, then start/resume an empty-queue worker or wake an active queue.
u32 CtScheduler::Add(CtScheduleCallback function, void *argument, u32 period, u8 oneShot, u8 fixedPeriod) {
  u32 id = ScFetchAdd(&nextId, 1);
  lock.Lock();
  bool empty = list.head->next == list.head;
  if (empty)
    elapsed.Restart();
  u32 deadline = period + elapsed.GetElapsedTime();
  CtScheduleNode *node = new CtScheduleNode;
  node->entry = {id, function, argument, oneShot, period, deadline, fixedPeriod};
  CtAppendSchedule(list.head, node);
  if (empty) {
    if (!running) {
      running = 1;
      worker.Start(this);
    }
    worker.WakeForWork();
  } else
    worker.Wake();
  lock.Unlock();
  return id;
}

// Remove the first matching registration under the list lock and return the original Boolean result.
bool CtScheduler::Cancel(u32 id) {
  lock.Lock();
  bool found = false;
  for (CtScheduleNode *node = list.head->next; node != list.head; node = node->next)
    if (node->entry.id == id) {
      CtEraseSchedule(node);
      found = true;
      break;
    }
  lock.Unlock();
  return found;
}

// Copy due callbacks under the list lock, update recurring deadlines, then deliver the copies after unlocking.
// Empty queues suspend the worker; nonempty queues retain original wraparound, clamping and alertable sleep arithmetic.
u32 CtScheduler::StartFunction(void *) {
  u32 id;
  worker.GetThreadId(&id);
  CtScheduleList batch;
  running = 1;
  while (!stop) {
    if (list.head->next != list.head) {
      if (stop)
        break;
      lock.Lock();
      CtScheduleNode *node = list.head->next;
      u32 minimum = UINT32_MAX, now = elapsed.GetElapsedTime();
      CtClearSchedule(batch.head);
      while (node != list.head) {
        CtScheduleNode *next = node->next;
        if (node->entry.deadline <= now) {
          CtScheduleNode *copy = new CtScheduleNode;
          copy->entry = node->entry;
          CtAppendSchedule(batch.head, copy);
          if (node->entry.oneShot) {
            CtEraseSchedule(node);
            node = next;
            continue;
          }
          node->entry.deadline = node->entry.period + (node->entry.fixedPeriod ? node->entry.deadline : now);
          if (node->entry.deadline < now)
            node->entry.deadline = UINT32_MAX;
        }
        u32 delta = node->entry.deadline - now;
        if (delta < minimum)
          minimum = delta;
        node = next;
      }
      lock.Unlock();
      for (node = batch.head->next; node != batch.head; node = node->next)
        node->entry.function(node->entry.argument);
      if (list.head->next != list.head) {
        if (stop)
          break;
        u32 spent = elapsed.GetElapsedTime() - now;
        u32 wait = spent < minimum ? minimum - spent : 10;
        ScThread::SleepCurrentThread(wait * 1000u);
        worker.GetThreadId(&id);
        continue;
      }
    }
    if (stop)
      break;
    worker.GetThreadId(&id);
    worker.WaitForWake();
    worker.GetThreadId(&id);
  }
  worker.GetThreadId(&id);
  running = 0;
  return 0;
}

CtScheduler CtSourceScheduler;

class CtSimulatorGuard {
public:
  // Enter the actual recursive simulator lock at the original scoped-lock boundary.
  explicit CtSimulatorGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the same lock when the original guarded scope ends.
  ~CtSimulatorGuard() {
    lock->Unlock();
  }

  ScLock *lock;
};

CtAgentList::CtAgentList() : head(new CtAgentNode) {
  head->next = head;
  head->previous = head;
}

// Unlink a live registry node after its caller has completed the required agent ownership operations.
static void CtEraseAgentNode(CtAgentNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

// Free only list storage; the simulator's shutdown method owns the separate agent Release calls.
CtAgentList::~CtAgentList() {
  while (head->next != head)
    CtEraseAgentNode(head->next);
  delete head;
}

// Append the actual agent pointer in registration order, preserving duplicate registrations.
void CtAgentList::Append(CtAgent *agent) {
  CtAgentNode *node = new CtAgentNode;
  node->next = head;
  node->previous = head->previous;
  node->agent = agent;
  head->previous->next = node;
  head->previous = node;
}

// Count live registry nodes using the original unsigned word width.
u32 CtAgentList::Count() const {
  u32 count = 0;
  for (CtAgentNode *node = head->next; node != head; node = node->next)
    ++count;
  return count;
}

CtPendingAgents::CtPendingAgents() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

CtPendingAgents::~CtPendingAgents() {
  delete[] begin;
}

u32 CtPendingAgents::Count() const {
  return begin ? (u32)(end - begin) : 0;
}

// Grow dedicated deferred-record storage and copy initialized fields, then append the original flag and receiver.
void CtPendingAgents::Append(CtAgent *agent, u8 adding) {
  if (end == storageEnd) {
    u32 count = Count(), capacity = begin ? (u32)(storageEnd - begin) : 0;
    if (count == kCtPendingAgentMaximumCount)
      ScFatal(SC_ERROR_NOMEM, "Simulator deferred-agent storage is too long");
    u32 next = capacity ? capacity * 2 : 1;
    if (next > kCtPendingAgentMaximumCount)
      next = kCtPendingAgentMaximumCount;
    CtPendingAgent *records = new CtPendingAgent[next];
    for (u32 i = 0; i < count; ++i) {
      records[i].adding = begin[i].adding;
      records[i].agent = begin[i].agent;
    }
    delete[] begin;
    begin = records;
    end = records + count;
    storageEnd = records + next;
  }
  end->adding = adding;
  end->agent = agent;
  ++end;
}

// Worker-queue commands retain their original DWORD selectors; calendar units have a separate enum.
enum CtSimCommand : u32 {
  kCtSimFinishYield = 0,
  kCtSimStart = 1,
  kCtSimStop = 2,
  kCtSimPhase0 = 3,
  kCtSimPhase1 = 4,
  kCtSimDay = 5,
  kCtSimWeek = 6,
  kCtSimMonth = 7,
  kCtSimYear = 8,
};

// Construct the six lists and deferred storage, then own a worker and recursive lock and retain the message service.
CtSimulator::CtSimulator()
    : worker(nullptr), semaphore(nullptr), startRequested(0), initialized(0), running(0), stopRequested(0),
      queue(nullptr), server(nullptr), references(0), lock(nullptr), yielding(0), city(nullptr) {
  worker = new ScThread;
  server = CtGetMessageServer();
  if (server)
    server->AddRef();
  lock = new ScLock;
  ResetIterating();
}

// Stop active simulation and shut down initialized services before releasing native synchronization and list storage.
CtSimulator::~CtSimulator() {
  if (running)
    CommandStop(nullptr);
  if (initialized)
    Shutdown();
  delete lock;
  lock = nullptr;
  delete worker;
  worker = nullptr;
  ScRelease(&server);
}

// Clear the genuine output before publishing and retaining the primary simulator interface for either accepted IID.
bool CtSimulator::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_ScUnknown || iid == IID_CtSimulatorInterface)
    *out = static_cast<CtSimulatorInterface *>(this);
  if (!*out)
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

// Retain the simulator and its module director while holding the actual recursive lock.
u32 CtSimulator::AddRef() {
  CtSimulatorGuard guard(lock);
  ++references;
  CtGetDirector()->AddRef();
  return references;
}

// Remove the module reference before decrementing ownership, unlocking before final receiver destruction.
u32 CtSimulator::Release() {
  lock->Lock();
  if (references) {
    CtGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    lock->Unlock();
    delete this;
    return 0;
  }
  lock->Unlock();
  return references;
}

// Retain the city, construct the start semaphore and acquire its actual owner-keyed queue before starting the worker.
bool CtSimulator::Init(CtCity *value) {
  stopRequested = 0;
  startRequested = 0;
  if (initialized)
    return false;
  city = ScAddRefNotNull(value);
  yielding = 0;
  semaphore = new ScSemaphore;
  semaphore->Init(1);
  ScQueueManager *manager = CtGetQueueManager();
  ScQueueKey queue_key = reinterpret_cast<ScQueueKey>(this);
  manager->AddQueue(queue_key);
  manager->GetQueue(queue_key, &queue);
  for (i32 phase = 0; phase < 6; ++phase)
    phases[phase] = lists + phase;
  initialized = 1;
  worker->Start(this);
  return true;
}

// Stop active simulation, flush deferred changes and release registrations before semaphore, queue and city ownership.
bool CtSimulator::Shutdown() {
  if (initialized) {
    if (running)
      CommandStop(nullptr);
    FlushPending();
    for (i32 phase = 0; phase < 6; ++phase) {
      CtAgentNode *head = phases[phase]->head;
      for (CtAgentNode *node = head->next; node != head; node = node->next)
        if (node->agent) {
          node->agent->Release();
          node->agent = nullptr;
        }
      while (head->next != head)
        CtEraseAgentNode(head->next);
    }
    ScDelete(&semaphore);
    if (queue) {
      CtGetQueueManager()->RemoveQueue(reinterpret_cast<ScQueueKey>(this), true);
      queue = nullptr;
    }
    ScRelease(&city);
    initialized = 0;
  }
  return true;
}

void CtSimulator::OnIdle() {
}

// Return the borrowed queue interface acquired from the queue manager, without retaining or wrapping it.
ScMessageQueue *CtSimulator::MessageQueue() {
  return queue;
}

u32 CtSimulator::GetLayerType() {
  return kCtSimulatorLayerType;
}

// Initialize the worker framework, then receive, poll and process persistent message storage until command two.
u32 CtSimulator::StartFunction(void *) {
  CtGetThreadFramework();
  ScMessage message = {};
  do {
    queue->GetFrontMessageBlock(&message);
    MaybeYield();
    WaitWhilePaused();
    ProcessMessage(&message);
  } while (message.type != kCtSimStop);
  return 1;
}

// Run the original backlog-yield and pause checks, retaining the immediate-pause method's Boolean success.
bool CtSimulator::DoSimImmediatePause() {
  MaybeYield();
  WaitWhilePaused();
  return true;
}

// Hold clock timers and append a completion message when either queue first crosses its original backlog threshold.
void CtSimulator::MaybeYield() {
  CtSimulatorGuard guard(lock);
  if (!yielding && (queue->GetMessageCount() > 3 || server->GetMessageQueueSize() > 500)) {
    city->HiddenClockPause();
    ScMessage message = {kCtSimFinishYield};
    queue->PushBack(message);
    yielding = 1;
  }
}

// Poll real city pause depth while running, lowering priority only during an actual wait and restoring it afterward.
void CtSimulator::WaitWhilePaused() {
  u32 waits = 0;
  i32 priority = 3;
  while (city->TotalPauseCount()) {
    if (!running || stopRequested)
      break;
    if (!waits) {
      priority = ScThread::CurrentPriority();
      ScThread::SetCurrentPriority(1);
    }
    ++waits;
    worker->Sleep(kCtPausePollSleepInterval);
  }
  if (waits)
    ScThread::SetCurrentPriority(priority);
}

void CtSimulator::FinishYield(ScMessage *) {
  city->HiddenClockUnpause();
  yielding = 0;
}

// Dispatch commands to their actual descending phase lists and retain the distinct monthly city-lock bracket.
bool CtSimulator::ProcessMessage(ScMessage *message) {
  switch (message->type) {
  case kCtSimFinishYield:
    FinishYield(message);
    break;
  case kCtSimStart:
    HandleStart();
    break;
  case kCtSimStop:
    HandleStop();
    break;
  case kCtSimPhase0:
    Iterate(0);
    break;
  case kCtSimPhase1:
    Iterate(1);
    break;
  case kCtSimDay:
    Iterate(2);
    city->SimCalendarNotification(2);
    break;
  case kCtSimWeek:
    Iterate(3);
    city->SimCalendarNotification(3);
    break;
  case kCtSimMonth:
    city->HiddenClockPause();
    Iterate(4);
    city->SimCalendarNotification(4);
    city->HiddenClockUnpause();
    break;
  case kCtSimYear:
    Iterate(5);
    city->SimCalendarNotification(5);
    break;
  default:
    return false;
  }
  return true;
}

// Drain the semaphore around queuing the start command, then restore its permits after worker acknowledgment.
bool CtSimulator::CommandStart(ScMessage *) {
  if (!running) {
    semaphore->GetSemaphoreAll();
    ScMessage message = {kCtSimStart};
    startRequested = 1;
    queue->PushBack(message);
    semaphore->GetSemaphoreAll();
    semaphore->ReleaseSemaphoreAll();
  }
  return true;
}

bool CtSimulator::CommandStop(ScMessage *) {
  if (running) {
    ScMessage message = {kCtSimStop};
    queue->PushFront(message);
    stopRequested = 1;
    worker->Wait();
  }
  return true;
}

// Announce startup, register calendar notifications and acknowledge a pending start after publishing running state.
void CtSimulator::HandleStart() {
  ScMessage message = {kCtMsg_SimulatorStarting};
  CtGetMessageServer()->MessageSend(message);
  if (initialized && !running) {
    CtClockInterface *clock = city->GetSimulationClock();
    if (clock) {
      clock->AddNotification(queue, CtClockUnit::Day, kCtSimDay);
      clock->AddNotification(queue, CtClockUnit::Week, kCtSimWeek);
      clock->AddNotification(queue, CtClockUnit::Month, kCtSimMonth);
      clock->AddNotification(queue, CtClockUnit::Year, kCtSimYear);
    }
    ResetIterating();
    running = 1;
  }
  if (startRequested) {
    startRequested = 0;
    semaphore->ReleaseSemaphoreAll();
  }
}

// End active simulation, preserving its distinct hidden-unpause call before removing clock registrations and messages.
void CtSimulator::HandleStop() {
  if (running) {
    if (yielding) {
      city->SimHiddenUnpause();
      yielding = 0;
    }
    CtClockInterface *clock = city->GetSimulationClock();
    if (clock) {
      clock->RemoveNotification(queue, CtClockUnit::Minute);
      clock->RemoveNotification(queue, CtClockUnit::Hour);
      clock->RemoveNotification(queue, CtClockUnit::Day);
      clock->RemoveNotification(queue, CtClockUnit::Week);
      clock->RemoveNotification(queue, CtClockUnit::Month);
      clock->RemoveNotification(queue, CtClockUnit::Year);
    }
    ResetIterating();
    queue->RemoveAll();
    running = 0;
  }
}

// Run each selected phase from highest to lowest, respecting deferred removals and the agent's own locking flag.
bool CtSimulator::Iterate(i32 requestedPhase) {
  if (!running)
    return false;
  for (i32 phase = requestedPhase; phase >= 0; --phase) {
    lock->Lock();
    SetIterating(phase);
    lock->Unlock();
    CtAgentNode *head = phases[phase]->head;
    for (CtAgentNode *node = head->next; node != head; node = node->next) {
      MaybeYield();
      WaitWhilePaused();
      lock->Lock();
      if (pending.begin == pending.end || !IsPendingRemoval(node->agent)) {
        lock->Unlock();
        bool agentLocking = node->agent->IsAgentLocking();
        if (!agentLocking)
          city->Lock();
        node->agent->Simulate(requestedPhase);
        if (!agentLocking)
          city->Unlock();
      } else
        lock->Unlock();
    }
    lock->Lock();
    ClearIterating(phase);
    FlushPending();
    lock->Unlock();
  }
  return true;
}

// Register an agent immediately or retain a deferred add when its current phase is being iterated.
bool CtSimulator::AddAgent(CtAgent *agent) {
  CtSimulatorGuard guard(lock);
  i32 phase = agent->MinimumExecutionInterval();
  if (phase >= 6)
    return false;
  if (IsIterating(phase)) {
    agent->AddRef();
    pending.Append(agent, 1);
    return true;
  }
  return AddNow(agent, phase);
}

// Remove the first registration or retain a deferred removal for the agent's current iterated phase.
bool CtSimulator::RemoveAgent(CtAgent *agent) {
  CtSimulatorGuard guard(lock);
  i32 phase = agent->MinimumExecutionInterval();
  if (phase >= 6)
    return false;
  if (IsIterating(phase)) {
    agent->AddRef();
    pending.Append(agent, 0);
    return true;
  }
  return RemoveNow(agent, phase);
}

// Apply the captured deferred-change range and release its retains, then discard the current vector contents.
void CtSimulator::FlushPending() {
  lock->Lock();
  u32 count = pending.Count();
  for (u32 i = 0; i < count; ++i) {
    CtAgent *agent = pending.begin[i].agent;
    u8 adding = pending.begin[i].adding;
    i32 phase = agent->MinimumExecutionInterval();
    if (adding)
      AddNow(agent, phase);
    else
      RemoveNow(agent, phase);
    agent->Release();
  }
  pending.end = pending.begin;
  lock->Unlock();
}

// Retain and append an agent, querying the real clock on the first registration and enabling minute/hour messages.
bool CtSimulator::AddNow(CtAgent *agent, i32 phase) {
  IsIterating(phase);
  agent->AddRef();
  lock->Lock();
  phases[phase]->Append(agent);
  u32 count = phases[phase]->Count();
  lock->Unlock();
  if (count == 1) {
    CtClockInterface *clock = city->GetSimulationClock();
    if (phase == 0)
      clock->AddNotification(queue, CtClockUnit::Minute, 3);
    else if (phase == 1)
      clock->AddNotification(queue, CtClockUnit::Hour, 4);
  }
  return true;
}

// Remove only the first matching registration and release it before disabling an empty minute/hour clock list.
bool CtSimulator::RemoveNow(CtAgent *agent, i32 phase) {
  IsIterating(phase);
  lock->Lock();
  CtAgentNode *head = phases[phase]->head, *node = head->next;
  while (node != head && node->agent != agent)
    node = node->next;
  if (node == head) {
    lock->Unlock();
    return false;
  }
  CtEraseAgentNode(node);
  lock->Unlock();
  agent->Release();
  if (!phases[phase]->Count()) {
    CtClockInterface *clock = city->GetSimulationClock();
    if (phase == 0)
      clock->RemoveNotification(queue, CtClockUnit::Minute);
    else if (phase == 1)
      clock->RemoveNotification(queue, CtClockUnit::Hour);
  }
  return true;
}

u8 CtSimulator::IsIterating(i32 phase) {
  CtSimulatorGuard guard(lock);
  return iterating[phase];
}

void CtSimulator::SetIterating(i32 phase) {
  CtSimulatorGuard guard(lock);
  iterating[phase] = 1;
}

void CtSimulator::ClearIterating(i32 phase) {
  CtSimulatorGuard guard(lock);
  iterating[phase] = 0;
}

void CtSimulator::ResetIterating() {
  for (i32 phase = 0; phase < 6; ++phase)
    ClearIterating(phase);
}

// Detect any deferred removal of this agent without changing pending order or reference ownership.
bool CtSimulator::IsPendingRemoval(CtAgent *agent) const {
  for (CtPendingAgent *entry = pending.begin; entry != pending.end; ++entry)
    if (entry->agent == agent && !entry->adding)
      return true;
  return false;
}

// Construct the registered class7000 simulator with its real worker, six agent lists and retained message service.
// This is the no-argument constructor phase at SIMCITY1000251D; its private nominal return declaration is unresolved.
ScUnknown *CtCreateSimulator() {
  return static_cast<CtSimulatorInterface *>(new CtSimulator);
}
