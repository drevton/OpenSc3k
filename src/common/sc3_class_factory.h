#ifndef SC3_COMMON_SC3_CLASS_FACTORY_H
#define SC3_COMMON_SC3_CLASS_FACTORY_H

#include "common/sc3_unknown.h"

// Original cIGZGameCommandFactory, IID20685AA3, acquired from the A4B5E41F command service.
// Command lookup and child registration are distinct from the COM service's GetClassObject operation.
class ScCommandFactory : public ScUnknown {
public:
  virtual bool GetCommand(u32 classId, u32 iid, void **out) = 0; // Original +12.
  virtual bool AddCommandFactory(ScCommandFactory *factory) = 0; // Original +16.
};

class UiCommandInterface;

// Original cRZAutoCmd owns one acquired command interface and releases it at the end of its scope.
// Copying retains the command; these are real game reference semantics, not a comparison adapter.
class ScAutoCommand {
public:
  ScAutoCommand(u32 identifier);
  ScAutoCommand(const ScAutoCommand &source);
  ~ScAutoCommand();
  ScAutoCommand *operator=(const ScAutoCommand &source);
  bool IsValid() const;
  UiCommandInterface *operator->();
  const UiCommandInterface *operator->() const;
  operator UiCommandInterface *();
  operator const UiCommandInterface *() const;
  bool GetCmd(u32 iid, void **out);

private:
  UiCommandInterface *command;
};

static_assert(sizeof(ScAutoCommand) == sizeof(void *));

#endif
