/*
 * data_ext - the class that gives a set of nodes a shape.
 *
 * The values live in nodes on the instance and nowhere else. Anything
 * that shows one links to it; nothing copies it, so there is never a
 * second holder to keep in step.
 *
 * This class answers the generic shape: an address is a name, the size
 * is how many cells there are, and writing out is the node walk every
 * instance already gets. A subclass answers where its own shape differs
 * and drops the rest.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "data_ext.h"

typedef struct InstanceData
{
	/* where to report back to, if whoever made us wants answers */
	NodeObj  owner;
	MsgId    msgID;
	char    *port;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* The cells are ordinary named properties, which is what carries them
   through clone and export: CloneData and the serializer both walk the
   property list under IsPortableProp, and a LONG-typed prop - a pointer
   to a private grid - is refused by it. So the storage IS properties. */
static NodeObj DataExt_Cell(NodeObj instance, char *at)
{
	NodeObj cell;

	if (!instance || !at || !at[0])
		return NULL;

	cell = GetPropNode(instance, at);
	if (!cell)
	{
		/* a cell exists because something referred to it */
		SetPropStr(instance, at, "");
		cell = GetPropNode(instance, at);
	}

	return cell;
}

/* how many cells: the portable properties, which is the same set that
   clone and export carry */
static int DataExt_Count(NodeObj instance)
{
	NodeObj prop;
	int     count = 0;

	for (prop = GetNextProp(instance); prop; prop = GetNextSibling(prop))
		if (IsPortableProp(instance, prop))
			count++;

	return count;
}

static int DataExt_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj       cell;
	char          dbg[200];

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
		case DATA_EXT_ADDRESS_MSG:
			cell = DataExt_Cell(instance, GetPropStr(data, "At"));
			if (!cell)
			{
				snprintf(dbg, sizeof(dbg), "ADDRESS on '%s' with no At",
						 GetNameStr(instance) ? GetNameStr(instance) : "?");
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);
				return rtrn_dropped;
			}
			SetPropLong(data, "Node", (long)cell);
			snprintf(dbg, sizeof(dbg), "ADDRESS '%s'.%s",
					 GetNameStr(instance) ? GetNameStr(instance) : "?",
					 GetPropStr(data, "At"));
			DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
			return rtrn_handled;

		case DATA_EXT_SHAPE_MSG:
			SetPropInt(data, "Count", DataExt_Count(instance));
			return rtrn_handled;
	}

	/* SERIALIZE and DESERIALIZE are not answered here: an instance with
	   no shape of its own is written out as its nodes, which is what
	   dropping the message leaves in place. */
	return rtrn_dropped;
}

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

	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgID = (MsgId)   GetPropLong(data, "MsgBase");
		local->port  = GetPropStr(data, "Port");
		local->port  = local->port ? strdup(local->port) : NULL;
	}

	SetName(instance, "data_ext");
	SetPropLong(instance, "local", (long)local);

	/* the entry node is a door, not an implementation: it punts, so the
	   answer comes from the class chain and a subclass that drops a
	   message falls through to us without either end knowing */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long)PuntToClass);

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

/* what a child that dropped a message falls through to */
static int DataExt_ClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	return DataExt_MessageFunc(instance, message, data);
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "data_ext");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* ON THE CLASS NODE. PuntToClass walks class nodes and reads ClassMsg
	   off each one; a ClassMsg left on the library node is somewhere the
	   walk never looks. */
	SetPropLong(ClassSelf, "ClassMsg", (long)DataExt_ClassMsg);

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

	SetName(temp, "data_ext");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "6f2a1c84-3b77-4d19-9e52-0a8c4f6d31b0");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
