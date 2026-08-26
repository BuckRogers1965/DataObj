/*
 * RadioGroup - a View with one behaviour: only one member at a time.
 *
 * It is not a new kind of control and it holds no list of buttons. It is a
 * panel you put controls in; when any member's Value becomes 1, every other
 * member's goes to 0. Which controls those are is whatever you dropped in -
 * checkboxes, buttons, anything with a Value.
 *
 * Two things it rides on, both already there: RegisterPath records a new
 * member's path on the CONTAINER as LastMember (object.c), which is an
 * ordinary property write and fans out - so a group hears something arrive
 * without a notification path existing for it. And MsgFromNode names the
 * property a delivery came from, which is how the one that just went to 1
 * is told apart from the ones that must go to 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "control.h"

typedef struct InstanceData
{
	int enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_unhandled;
}

/* watch this member's Value, so its going to 1 reaches us */
static void Radio_Watch(NodeObj group, NodeObj member)
{
	char dbg[300];

	if (!group || !member || member == group)
		return;
	if (!GetPropNode(member, "Value"))
		return;					/* nothing to be exclusive about */

	/* AddSubscription refuses a duplicate {instance, port}, so re-running
	   this over a member already watched costs nothing */
	Connect(member, "Value", group, "Member");

	snprintf(dbg, sizeof(dbg), "RADIO '%s' watches '%s'.Value",
			 GetPropStr(group, "Name") ? GetPropStr(group, "Name") : "?",
			 GetPropStr(member, "Name") ? GetPropStr(member, "Name") : "?");
	DebugPrint(dbg, __FILE__, __LINE__, WIRE);
}

/* every instance whose Container is this group */
static void Radio_EachMember(NodeObj group, void (*fn)(NodeObj, NodeObj))
{
	NodeObj inst;
	char    me[300], *cont;

	if (!group || !PathOfInstance(group, me, sizeof(me)))
		return;

	for (inst = FirstInstance(); inst; inst = NextInstance(inst))
	{
		cont = GetPropStr(inst, "Container");
		if (cont && !strcmp(cont, me))
			fn(group, inst);
	}
}

/* clear this one unless it is the one that just went on */
static NodeObj Radio_Chosen;

static void Radio_ClearOther(NodeObj group, NodeObj member)
{
	NodeObj owner = member, val;

	(void) group;

	val = ResolvePort(&owner, "Value");
	if (!val || val == Radio_Chosen)
		return;						/* the one that was just set stays set */

	if (GetValueInt(val))
		SetOrDeliverProp(member, "Value", "0");
}

/* a member's Value arrived. Only a 1 means anything: a 0 is what we just
   wrote to everything else, and acting on it would chase its own tail. */
int Radio_OnMember(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char dbg[300];

	if (message == msg_eof || !data)
		return rtrn_handled;
	if (local && !local->enabled)
		return rtrn_handled;
	if (!GetValueInt(data))
		return rtrn_handled;

	/* WHICH ONE WENT ON: the property the delivery came from, not a name we
	   guessed and not an owner we looked up */
	Radio_Chosen = MsgFromNode();

	snprintf(dbg, sizeof(dbg), "RADIO '%s': one went on, clearing the rest",
			 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?");
	DebugPrint(dbg, __FILE__, __LINE__, WIRE);

	Radio_EachMember(instance, Radio_ClearOther);
	Radio_Chosen = NULL;

	return rtrn_handled;
}

/* something landed in this container - RegisterPath wrote its path here */
int Radio_OnLastMember(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj member;

	if (message == msg_eof || !data)
		return rtrn_handled;

	member = ResolvePath(GetValueStr(data));
	if (member)
		Radio_Watch(instance, member);

	return rtrn_handled;
}

int Radio_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* members restored by a load or a clone were never announced to us - they
   were already there - so pick them up once the group is settled */
int Radio_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	Radio_EachMember(instance, Radio_Watch);
	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "RadioGroup");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Radio_Activate);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Radio_OnEnable);

	SetPropStr(instance, "ReservedViewResizeable", "1");

	/* a grouping is drawn where it sits, not as an icon you open */
	SetPropStr(instance, "ReservedViewEmbedded", "1");

	/* where members report in, and where their arrival is announced */
	SetPropStr(instance, "Member", "0");
	port = GetPropNode(instance, "Member");
	SetPropLong(port, "OnMsg", (long)Radio_OnMember);

	/* A PROPERTY'S OWN HANDLER DOES NOT FIRE ON A PLAIN WRITE. RegisterPath
	   announces a new member with SetPropStr(container, "LastMember", path),
	   which stores and fans out to SUBSCRIBERS - it does not deliver to the
	   property's own OnMsg. So the group subscribes to its own LastMember,
	   like anything else that wants to hear about it. */
	SetPropStr(instance, "LastMember", "");

	SetPropStr(instance, "Arrived", "");
	port = GetPropNode(instance, "Arrived");
	SetPropLong(port, "OnMsg", (long)Radio_OnLastMember);

	Connect(instance, "LastMember", instance, "Arrived");

	InitPosition(instance);
	SetPropInt(instance, "W", 190);
	SetPropInt(instance, "H", 220);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "RadioGroup");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "View");

	/* no PublishShow: this renders as what it is, a View - the client
	   resolves a renderer by class and falls back to the parent */
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State", PROP_LED, "1");
	PublishProp(ClassSelf, "ReservedViewResizeable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "ReservedViewEmbedded", PROP_CHECKBOX, "1");
	PublishPosition(ClassSelf);

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "RadioGroup");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b1f4c2a6-7d38-4e91-9c05-3a6e8b2d47f1");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "control.object", "Control", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
