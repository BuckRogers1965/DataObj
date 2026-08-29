/*
 * Skeleton - a DATA SHAPE: a child of data_ext.
 *
 * A data object holds values in nodes, and the nodes are the ONLY place a
 * value lives - anything showing one holds a link to it, never a copy. A
 * shape says two things and nothing else: how those nodes are ADDRESSED, and
 * how they are WRITTEN OUT. Answer where you differ, drop the rest, and the
 * walk carries on up to data_ext and then to the plain node tree.
 *
 * This template is a one-dimensional shape - an indexed list - because that
 * is the smallest thing that has a real address and a real text form. A grid
 * (objects/table) is the same module with two coordinates instead of one.
 *
 * WHAT A SHAPE IS NOT: it is not a control, it is not a member of a view, and
 * it has no panel, no X/Y and nothing to lay out. If you are laying anything
 * out you are writing a widget, and the widget is the thing that OWNS one of
 * these.
 *
 * It IS in the tree like everything else - a registered instance, a node with
 * properties. What its owner holds is a long whose value is the instance.
 * What is private is the GRID, and the grid is nothing but a grid of this
 * node's own properties reached by name: nothing places it, nothing gives it
 * a path, so nobody else addresses them. The long cannot reach a file
 * (IsPortableProp refuses it); the properties travel with a clone and an
 * export.
 *
 * Descends from data_ext, which descends from Object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "data_ext.h"

/* Per-instance C state. The index is a SHORTCUT, never a second copy - the
   grid IS the properties, and this array only saves looking one up by name.
   It lives on `local` as a LONG, so IsPortableProp refuses it and it can
   never reach a clone or a file. */
typedef struct InstanceData
{
	NodeObj *entry;			/* entry[i] -> the node holding item i */
	int      indexed;		/* how many the index covers */

	/* whoever created this and how it hears back - the {owner, base, port}
	   handed to InstanceStart. A shape rarely reports anything, but the
	   convention costs nothing and every other object follows it. */
	NodeObj  owner;
	MsgId    msgID;
	char    *port;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

/* ---- addressing ------------------------------------------------------

   THE KEY IS THE ADDRESS, spelled as a property name. Entries are ordinary
   named properties on the instance, so a clone copies them and an export
   writes them with no help from this module. Name them something a person
   can read in a dump: a grid says A1, this says E0. */

static void Skeleton_Key(char *buf, int size, int index)
{
	snprintf(buf, size, "E%d", index);
}

static int Skeleton_ParseKey(char *name, int *index)
{
	char *end;
	long  v;

	if (!name || name[0] != 'E' || !name[1])
		return 0;

	v = strtol(name + 1, &end, 10);
	if (*end || v < 0)
		return 0;

	*index = (int) v;
	return 1;
}

/* the node holding item i, made if it is not there yet. SPARSE: an entry
   with no value has no node at all, which is what keeps a big shape cheap -
   so only ask for one when something is actually being stored. */
static NodeObj Skeleton_Entry(NodeObj instance, int index, int create)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char          key[32];
	NodeObj       node;

	if (index < 0)
		return NULL;

	/* the index is self-healing: a miss just looks the entry up by name and
	   refills, so nothing has to keep it in step with the properties */
	if (local && local->entry && index < local->indexed && local->entry[index])
		return local->entry[index];

	Skeleton_Key(key, sizeof(key), index);
	node = GetPropNode(instance, key);

	if (!node && create)
	{
		SetPropStr(instance, key, "");
		node = GetPropNode(instance, key);
		if (index >= GetPropInt(instance, "Count"))
			SetPropInt(instance, "Count", index + 1);
	}

	if (node && local)
	{
		if (index >= local->indexed)
		{
			int want = index + 1;

			local->entry = realloc(local->entry, want * sizeof(NodeObj));
			memset(local->entry + local->indexed, 0,
				   (want - local->indexed) * sizeof(NodeObj));
			local->indexed = want;
		}
		local->entry[index] = node;
	}

	return node;
}

/* ---- the five verbs -------------------------------------------------- */

/* DROP: throw the contents away, keep the shape. The nodes really go, so
   whatever indexes them has to let go in the same breath - that is why a
   caller cannot do this for you. */
static void Skeleton_Clear(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj       prop, next;
	int           index, dropped = 0;
	char          dbg[160];

	if (local)
	{
		free(local->entry);
		local->entry = NULL;
		local->indexed = 0;
	}

	for (prop = GetNextProp(instance); prop; prop = next)
	{
		next = GetNextSibling(prop);
		if (!GetNameStr(prop) || !Skeleton_ParseKey(GetNameStr(prop), &index))
			continue;

		RemoveProp(instance, prop);
		DelNode(prop);
		dropped++;
	}

	snprintf(dbg, sizeof(dbg), "cleared %d entr(ies) from '%s'", dropped,
			 GetNameStr(instance) ? GetNameStr(instance) : "?");
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* SERIALIZE: how this shape stores itself, as text. THIS IS THE FILE FORMAT,
   so write what DEFINES an entry, not something derived from it - a cell that
   is computed writes its formula, never the answer, or a load restores a
   number and loses the question. */
static char *Skeleton_Write(NodeObj instance)
{
	char *buf = NULL;
	int   len = 0, cap = 0, count, i;

	count = GetPropInt(instance, "Count");

	for (i = 0; i < count; i++)
	{
		NodeObj entry = Skeleton_Entry(instance, i, 0);
		char   *val = entry ? GetValueStr(entry) : NULL;
		int     need;

		if (!val)
			val = "";

		need = len + (int) strlen(val) + 2;
		if (need > cap)
		{
			while (cap < need)
				cap = cap ? cap * 2 : 256;
			buf = realloc(buf, cap);
		}
		strcpy(buf + len, val);
		len += (int) strlen(val);
		buf[len++] = '\n';
		buf[len] = 0;
	}

	return buf;
}

/* DESERIALIZE: become this text. A representation and its inverse ship
   together or not at all - they cannot be allowed to drift apart. */
static void Skeleton_Read(NodeObj instance, char *text)
{
	char *copy, *p, *start;
	int   index = 0;

	if (!instance || !text)
		return;

	Skeleton_Clear(instance);

	copy = strdup(text);
	if (!copy)
		return;

	start = copy;
	for (p = copy; ; p++)
	{
		char held = *p;

		if (held != '\n' && held != '\0')
			continue;
		if (held == '\0' && p == start)
			break;

		*p = 0;
		/* sparse: an empty field is an entry that is not there */
		if (*start)
		{
			NodeObj entry = Skeleton_Entry(instance, index, 1);

			if (entry)
				SetValueStr(entry, start);
		}
		index++;

		if (held == '\0')
			break;
		start = p + 1;
	}

	if (index > GetPropInt(instance, "Count"))
		SetPropInt(instance, "Count", index);

	free(copy);
}

/* ANSWER WHERE YOU DIFFER, DROP THE REST. Anything returned as dropped
   carries on up the class chain to data_ext and then to Object, which is how
   a shape gets the plain node-tree behaviour for everything it has no
   opinion about. */
static int Skeleton_ClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj entry;
	char   *text;
	char    dbg[200];

	switch (message)
	{
		case DATA_EXT_ADDRESS_MSG:
			/* WHAT AN ADDRESS IS BELONGS TO THE SHAPE. A grid reads Row and
			   Col; this reads Index; a record would read a field name. The
			   answer always comes back on the data node's "Node". */
			entry = Skeleton_Entry(instance, GetPropInt(data, "Index"), 1);
			if (!entry)
			{
				snprintf(dbg, sizeof(dbg), "ADDRESS on '%s' out of range: %d",
						 GetNameStr(instance) ? GetNameStr(instance) : "?",
						 GetPropInt(data, "Index"));
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);
				return rtrn_dropped;
			}
			SetPropLong(data, "Node", (long)entry);
			return rtrn_handled;

		case DATA_EXT_SHAPE_MSG:
			/* how much of me there is, in the terms of MY shape */
			SetPropInt(data, "Count", GetPropInt(instance, "Count"));
			return rtrn_handled;

		case DATA_EXT_DROP_MSG:
			Skeleton_Clear(instance);
			return rtrn_handled;

		case DATA_EXT_SERIALIZE_MSG:
			text = Skeleton_Write(instance);
			SetPropStr(data, "Text", text ? text : "");
			if (text)
				free(text);
			return rtrn_handled;

		case DATA_EXT_DESERIALIZE_MSG:
			Skeleton_Read(instance, GetPropStr(data, "Text"));
			return rtrn_handled;
	}

	return rtrn_dropped;
}

/* ---- lifecycle ------------------------------------------------------- */

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

	SetName(instance, "Skeleton");
	SetPropLong(instance, "local", (long)local);

	/* the extent, as an ordinary property, so a clone and an export carry it
	   along with the entries */
	SetPropInt(instance, "Count", 0);

	/* THE DOOR ONTO THE CLASS CHAIN: one entry named "Msg" whose handler is
	   PuntToClass itself. This is what makes the class chain answer for this
	   instance, so what Skeleton drops data_ext gets asked. Without it the
	   verbs reach nothing at all. */
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
		free(local->entry);			/* the index, not the entries */
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

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "data_ext");

	/* ON THE CLASS NODE, not the library node: PuntToClass walks class nodes
	   and reads ClassMsg off each one. A shape that writes it on its library
	   node is never reached, and the failure is silent. */
	SetPropLong(ClassSelf, "ClassMsg", (long)Skeleton_ClassMsg);

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

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "data_ext.object", "data_ext", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
