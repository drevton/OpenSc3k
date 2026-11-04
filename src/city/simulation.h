#ifndef SC3_CITY_SIMULATION_H
#define SC3_CITY_SIMULATION_H

#include "city/api.h"
#include "city/sc_stopwatch.h"
#include "common/sc3.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "resources/resource.h"

// Concrete default city agent registered as class80F6A7ED; the retained type supplies simulation and scheduling rules.
class CtAgentImpl : public CtAgent {
public:
  CtAgentImpl();
  virtual ~CtAgentImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  CtAgentType *type;
  u32 references;
};

// Retail cIGZTreeNode (CodeView type814A), matching the installed SIMCITY table10013E44.
// Input references use explicit pointers; original method order and return contracts are retained.
class ScTreeNode : public ScUnknown {
public:
  virtual void DeleteChildren() = 0;
  virtual void SetNodeText(ScStringView *text) = 0;
  virtual ScStringView *GetNodeText() = 0;
  virtual void SetNodeData(u32 value) = 0;
  virtual u32 *GetNodeData() = 0;
  virtual ScTreeNode *GetParent() = 0;
  virtual ScTreeNode *GetNextSibling() = 0;
  virtual ScTreeNode *GetPrevSibling() = 0;
  virtual ScTreeNode *GetFirstChild() = 0;
  virtual ScTreeNode *GetLastChild() = 0;
  virtual ScTreeNode *GetFirstSibling() = 0;
  virtual ScTreeNode *GetLastSibling() = 0;
  virtual ScTreeNode *GetBottomLeftChild() = 0;
  virtual ScTreeNode *GetBottomRightChild() = 0;
  virtual ScTreeNode *GetRoot() = 0;
  virtual ScTreeNode *GetNextInDisplayOrder() = 0;
  virtual ScTreeNode *GetPrevInDisplayOrder() = 0;
  virtual ScTreeNode *Search(u32 data) = 0;
  virtual ScTreeNode *GetNextDrawableNode() = 0;
  virtual bool OnNextSearchNode(u32 data, ScTreeNode *node) = 0;
  virtual void SetParent(ScTreeNode *node) = 0;
  virtual void SetNextSibling(ScTreeNode *node) = 0;
  virtual void SetPrevSibling(ScTreeNode *node) = 0;
  virtual void SetFirstChild(ScTreeNode *node) = 0;
  virtual bool AddChild(ScTreeNode *child, ScTreeNode *position) = 0;
  virtual void DetachFromTree() = 0;
  virtual void DeleteAllChildren() = 0;
  virtual bool HasChildren() = 0;
  virtual bool IsDescendant(ScTreeNode *node) = 0;
  virtual bool IsAncestor(ScTreeNode *node) = 0;
  virtual bool IsSibling(ScTreeNode *node) = 0;
  virtual u32 GetDistanceFromRoot() = 0;
  virtual u32 GetNumDescendents() = 0;
  virtual bool IsExpanded() = 0;
  virtual void SetExpanded(bool state) = 0;
};

// Actual named-node implementation forms the agent type's secondary base and owns only its name.
class ScNamedNode : public ScTreeNode {
public:
  ScNamedNode();
  virtual ~ScNamedNode();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void DeleteChildren() override;
  void DeleteAllChildren() override;
  ScStringView *GetNodeText() override;
  void SetNodeText(ScStringView *text) override;
  u32 *GetNodeData() override;
  void SetNodeData(u32 value) override;
  bool HasChildren() override;
  bool AddChild(ScTreeNode *child, ScTreeNode *position) override;
  void DetachFromTree() override;
  ScTreeNode *GetParent() override;
  ScTreeNode *GetNextSibling() override;
  ScTreeNode *GetPrevSibling() override;
  ScTreeNode *GetFirstChild() override;
  ScTreeNode *GetRoot() override;
  ScTreeNode *GetFirstSibling() override;
  ScTreeNode *GetLastSibling() override;
  ScTreeNode *GetLastChild() override;
  ScTreeNode *GetBottomLeftChild() override;
  ScTreeNode *GetBottomRightChild() override;
  bool IsExpanded() override;
  void SetExpanded(bool state) override;
  ScTreeNode *GetNextDrawableNode() override;
  ScTreeNode *GetNextInDisplayOrder() override;
  ScTreeNode *GetPrevInDisplayOrder() override;
  bool IsDescendant(ScTreeNode *node) override;
  bool IsAncestor(ScTreeNode *node) override;
  bool IsSibling(ScTreeNode *node) override;
  u32 GetDistanceFromRoot() override;
  u32 GetNumDescendents() override;
  ScTreeNode *Search(u32 data) override;
  bool OnNextSearchNode(u32 data, ScTreeNode *node) override;
  void SetParent(ScTreeNode *node) override;
  void SetNextSibling(ScTreeNode *node) override;
  void SetPrevSibling(ScTreeNode *node) override;
  void SetFirstChild(ScTreeNode *node) override;
  u32 references;
  ScTreeNode *parentNode, *siblingNode, *previousSiblingNode, *firstChildNode;
  ScString nodeName;
  u32 nodeData;
  bool expansionState;
};

// SIMCITY's Gregorian helpers retain 32-bit unsigned arithmetic, including wraparound.
i32 CtCalendarLeapYear(u32 year);
i32 CtCalendarValid(u32 month, u32 day, u32 year);
u32 CtCalendarEncode(u32 month, u32 day, u32 year);

class CtTime;
class ScStringView;

// Complete cIGZDate contract from retail CodeView3A3F and installed SIMGEOM table1002AA60.
// Borrowed const-reference operator results use const pointers under the project source-spelling rule.
// Overload declaration order preserves the Microsoft table groups; postfix lifetimes remain unresolved.
class CtDateInterface : public ScUnknown {
public:
  virtual void Set(u32 day, u32 year) = 0;
  virtual void Set(u32 month, u32 day, u32 year) = 0;
  virtual void Set(const CtTime &time) = 0;
  virtual void Set(u32 serial) = 0;
  virtual void Set(const char *month, u32 day, u32 year) = 0;
  virtual void Copy(const CtDateInterface &source) = 0;
  virtual void Clone(CtDateInterface **out) = 0;
  virtual i32 Between(const CtDateInterface &first, const CtDateInterface &second) const = 0;
  virtual i32 CompareTo(const CtDateInterface &other) const = 0;
  virtual u32 WeekDay() const = 0;
  virtual u32 Year() const = 0;
  virtual u32 DayOfYear() const = 0;
  virtual u32 DayOfMonth() const = 0;
  virtual u32 FirstDayOfMonth() const = 0;
  virtual u32 FirstDayOfMonth(u32 month) const = 0;
  virtual u32 Hash() const = 0;
  virtual i32 IsValid() const = 0;
  virtual i32 Leap() const = 0;
  virtual void MaxDate(const CtDateInterface &other, CtDateInterface *out) const = 0;
  virtual void MinDate(const CtDateInterface &other, CtDateInterface *out) const = 0;
  virtual u32 Month() const = 0;
  virtual void Previous(u32 weekday, CtDateInterface *out) const = 0;
  virtual u32 YearLastTwoDigits() const = 0;
  virtual u32 DayNumber() const = 0;
  virtual i32 operator<(const CtDateInterface &other) const = 0;
  virtual i32 operator<=(const CtDateInterface &other) const = 0;
  virtual i32 operator>(const CtDateInterface &other) const = 0;
  virtual i32 operator>=(const CtDateInterface &other) const = 0;
  virtual i32 operator==(const CtDateInterface &other) const = 0;
  virtual i32 operator!=(const CtDateInterface &other) const = 0;
  virtual u32 operator-(const CtDateInterface &other) const = 0;
  virtual u32 operator-(i32 days) const = 0;
  virtual u32 operator+(const CtDateInterface &other) const = 0;
  virtual u32 operator+(i32 days) const = 0;
  virtual const CtDateInterface *operator++() = 0;
  virtual const CtDateInterface *operator++(int) = 0;
  virtual const CtDateInterface *operator--() = 0;
  virtual const CtDateInterface *operator--(int) = 0;
  virtual const CtDateInterface *operator+=(i32 days) = 0;
  virtual const CtDateInterface *operator-=(i32 days) = 0;
  virtual void DateString(ScStringView *out, u32 format) const = 0;
  virtual i32 DayWithinMonth(u32 month, u32 day, u32 year) = 0;
  virtual u32 DaysInYear(u32 year) = 0;
  virtual u32 Jday(u32 month, u32 day, u32 year) = 0;
  virtual i32 LeapYear(u32 year) = 0;
};

// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.
/*
 * sc3k-gzcom-dll - an open-source DLL Plugin SDK for SimCity 3000
 *
 * cISC3CityLayerManipulator.h
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
 * License along with this library; see evidence/SIMCITY/sdk-license.txt.
 */

class CtAgent;
class ScStringView;

class CtLayerManipulator : public ScUnknown {
public:
  virtual bool Apply(CtAgent *agent) = 0;
  virtual bool IsLockingCity() = 0;
  virtual bool DebugInstanceTag(ScStringView *out) = 0;
  virtual bool DebugClassTag(ScStringView *out) = 0;
  virtual bool DebugTypeTag(ScStringView *out) = 0;
};

typedef i32 (*CtScheduleCallback)(void *argument);

struct CtScheduleEntry {
  u32 id;
  CtScheduleCallback function;
  void *argument;
  u8 oneShot;
  u32 period, deadline;
  u8 fixedPeriod;
};

struct CtScheduleNode {
  CtScheduleNode *volatile next, *volatile previous;
  CtScheduleEntry entry;
};

// Native ownership for the recovered scheduler's circular callback list, replacing only generic list machinery.
class CtScheduleList {
public:
  CtScheduleList();
  ~CtScheduleList();
  CtScheduleNode *head;
};

// The actual process scheduler has a callback list, elapsed timer, worker and recursive list lock.
// The common synchronization boundary preserves backend publication while list contents stay lock-protected.
class CtScheduler : public ScThreadTarget {
public:
  CtScheduler();
  ~CtScheduler();
  u32 Add(CtScheduleCallback function, void *argument, u32 period, u8 oneShot, u8 fixedPeriod);
  bool Cancel(u32 id);
  u32 StartFunction(void *argument) override;
  CtScheduleList list;
  ScStopwatch elapsed;
  ScThread worker;
  ScAtomicU32 nextId;
  ScLock lock;
  ScThreadFlag stop, running;
};

extern CtScheduler CtSourceScheduler;

class ScSemaphore;
class ScLock;
class ScMessageQueue;
class ScMessageServer;

struct CtAgentNode {
  CtAgentNode *next, *previous;
  CtAgent *agent;
};

class CtAgentList {
public:
  CtAgentList();
  ~CtAgentList();
  void Append(CtAgent *agent);
  u32 Count() const;
  CtAgentNode *head;
};

struct CtPendingAgent {
  u8 adding;
  CtAgent *agent;
};

// Dedicated storage for deferred agent changes, retaining the original three-pointer member shape.
class CtPendingAgents {
public:
  CtPendingAgents();
  ~CtPendingAgents();
  void Append(CtAgent *agent, u8 adding);
  u32 Count() const;
  CtPendingAgent *begin, *end, *storageEnd;
};

class CtSimulator : public CtSimulatorInterface, public ScThreadTarget {
public:
  CtSimulator();
  virtual ~CtSimulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city) override;
  bool Shutdown() override;
  void OnIdle() override;
  ScMessageQueue *MessageQueue() override;
  bool ProcessMessage(ScMessage *message) override;
  bool CommandStart(ScMessage *message) override;
  bool CommandStop(ScMessage *message) override;
  bool AddAgent(CtAgent *agent) override;
  bool RemoveAgent(CtAgent *agent) override;
  u32 GetLayerType() override;
  bool DoSimImmediatePause() override;
  u32 StartFunction(void *argument) override;
  bool Iterate(i32 phase);
  void MaybeYield();
  void WaitWhilePaused();
  void FinishYield(ScMessage *message);
  void HandleStart();
  void HandleStop();
  void FlushPending();
  bool AddNow(CtAgent *agent, i32 phase);
  bool RemoveNow(CtAgent *agent, i32 phase);
  u8 IsIterating(i32 phase);
  void SetIterating(i32 phase);
  void ClearIterating(i32 phase);
  void ResetIterating();
  bool IsPendingRemoval(CtAgent *agent) const;
  ScThread *worker; // Owned; stopped before destruction.
  ScSemaphore *semaphore; // Owned during initialization; retired by Shutdown.
  volatile u8 startRequested, initialized, running, stopRequested;
  ScMessageQueue *queue; // Borrowed from the queue manager until its entry is removed.
  ScMessageServer *server; // Retained by the constructor and released after the worker is destroyed.
  u32 references;
  ScLock *lock;
  volatile u8 yielding;
  CtCity *city; // Retained by Init and released by Shutdown.
  CtAgentList lists[6];
  CtAgentList *phases[6];
  volatile u8 iterating[6];
  CtPendingAgents pending;
};

class CtDate;

// Single-word cRZTime receiver used by the simulation clock. Its date conversion uses the game's 1901 epoch.
class CtTime {
public:
  CtTime();
  explicit CtTime(u32 value);
  CtTime(const CtDate &date, u32 hour, u32 minute, u32 second);
  i32 IsDst() const;
  u32 Hour() const;
  u32 Minute() const;
  static CtTime DstStart(u32 year);
  static CtTime DstEnd(u32 year);
  u32 timestamp;
};

// Dedicated three-pointer storage replaces only the original phase vector's pointer operations.
class CtManipulatorStorage {
public:
  CtManipulatorStorage();
  ~CtManipulatorStorage();
  void Append(CtLayerManipulator *manipulator);
  void Erase(CtLayerManipulator **position);
  void Clear();
  CtLayerManipulator **begin, **end, **storageEnd;
};

// Original184-byte simulation type has a real primary agent-type interface and secondary named tree-node base.
// The secondary node view and primary type hierarchy retain their separate borrowed identities.
class CtAgentTypeImpl : public CtAgentType, public ScNamedNode {
public:
  CtAgentTypeImpl();
  ~CtAgentTypeImpl() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScTreeNode *AsIGZTreeNode() override;
  u32 Id() const override;
  bool AttachManipulator(CtLayerManipulator *manipulator, i32 flags) override;
  bool ReleaseManipulator(CtLayerManipulator *manipulator, i32 flags) override;
  void ReleaseAllManipulators() override;
  bool Simulate(CtAgent *agent, i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  CtAgentType *Parent() const override;
  CtAgentType *FirstChild() const override;
  CtAgentType *Sibling() const override;
  void GetName(RsKey *out) override;
  bool SetId(u32 id) override;
  bool SetName(const RsKey &key) override;
  bool SetParent(CtAgentType *parent) override;
  bool SetFirstChild(CtAgentType *child) override;
  bool SetSibling(CtAgentType *sibling) override;
  bool DebugInstanceTag(ScStringView *out) override;
  void DebugSetInstanceTag(ScStringView *tag) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool hasManipulators;
  u32 typeId;
  CtAgentType *parentType, *siblingType, *firstChildType;
  CtManipulatorStorage storage[6], *phases[6];
  RsKey name;
};

#endif
