/*
 * Skeleton - a plain OBJECT: function, with no presentation of any kind.
 *
 * It has no controls, no panel, no place on a canvas, and it is never
 * serialized. Its whole interface is skeleton.h: a set of message ids and the
 * macros that wrap them. Nothing outside this file knows what its state looks
 * like, and nothing can reach into it - a driver holds a handle and sends
 * messages, and that is all.
 *
 * Descends from Object, which is the end of the class chain.
 *
 * Copy this when you are writing FUNCTION - a socket, a resolver, a codec, an
 * interpreter. If people also need to operate it by hand, write a Control or a
 * Widget as a SEPARATE module that owns one of these and drives it (see
 * objects/udp + objects/udpport). Do not grow controls onto this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "skeleton.h"

/* everything this object is, lives here - defined in the .c, never the .h, so
   a driver cannot cast a handle and reach in even if it tries */
typedef struct InstanceData
{
	int      state;

	/* where to report back to, chosen by whoever created us: an owner, the
	   message id it wants answers on, and the property to deliver them to.
	   The owner picks the base so one owner can hold several of us and still
	   tell the answers apart. */
	NodeObj  owner;
	MsgId    msgID;
	char    *port;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* answer the owner: base + ordinal, so SKELETON_DONE_CALLBACK and friends
   arrive as distinct messages on the owner's own port */
static void Skeleton_Report(InstanceData *local, int ordinal, char *text)
{
	NodeObj chunk;

	if (!local || !local->owner || !local->port)
		return;

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, text ? text : "");
	DeliverMsg(local->owner, local->port, local->msgID + ordinal, chunk);
	DelNode(chunk);
}

/* THE message function: the object's whole behaviour, switched on the ids in
   skeleton.h. This is what the "Msg" entry node's OnMsg points at, so every
   macro in the header lands here. */
static int Skeleton_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
		case SKELETON_START_MSG:
			local->state = 1;
			return rtrn_handled;

		case SKELETON_STOP_MSG:
			local->state = 0;
			return rtrn_handled;

		case SKELETON_DO_MSG:
			/* the work. Report back rather than storing an answer where
			   someone has to go and look for it. */
			Skeleton_Report(local, SKELETON_DONE, GetValueStr(data));
			return rtrn_handled;
	}

	return rtrn_dropped;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj       instance = NewNode(INTEGER);
	NodeObj       entry;
	InstanceData *local;

	(void) message;

	local = malloc(sizeof(InstanceData));
	memset(local, 0, sizeof(InstanceData));

	/* the callback address the creator handed over, in the data node it
	   passed to InstanceStart. No properties are published for this: the
	   creator already knows what it asked for. */
	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgID = (MsgId)   GetPropLong(data, "MsgBase");
		local->port  = GetPropStr(data, "Port");
		local->port  = local->port ? strdup(local->port) : NULL;
	}

	SetName(instance, "Skeleton");
	SetPropLong(instance, "local", (long)local);

	/* ONE entry node, whose OnMsg is the message function. Every macro in
	   skeleton.h delivers to "Msg" - that is the whole surface. */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long)Skeleton_MessageFunc);

	/* NO PublishProp, NO PublishPosition, NO InitPosition: nothing here is
	   presented, addressed by path, or saved. Adding any of those makes this
	   a Control by accident. */

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
	{
		if (local->port)
			free(local->port);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Skeleton");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	/* what it is, and at what version. A dependent asking for Skeleton 1 0
	   is checked against these two. */
	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* nothing is published: the interface is skeleton.h, not a set of
	   properties */

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

	SetName(temp, "Skeleton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "REPLACE-WITH-A-FRESH-UUID");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	/* a plain object needs only the core's Object class. Nothing else, ever -
	   if you find yourself adding control.object or widget.object here, what
	   you are writing is not a plain object. */
	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
