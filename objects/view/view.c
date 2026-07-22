
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

View object: the container primitive - a first-class object on the
palette exactly like LED or Button. It carries no Value of its own - it
is not a data-holding leaf - and, like every other placeable object,
its own position is just PublishPosition/InitPosition (object.c), the
same opt-in helper any object calls for itself. There is no separate
membership mechanism here (no Slot table, no AddSlot) - which instance
is "in" a View is Container, an ordinary property on the CHILD (see
PublishPosition/InitPosition, object.c), not something the View itself
tracks a list of. View does not need to know about its children any
more than a Reader needs to know about its Filename textbox - the
wiring already says what's connected to what.

Two properties of its own beyond position: Resizeable (a client shows a
resize handle unless this reads "0") and Mode - empty means "whatever
the session's global mode currently is", set to anything else and every
click inside this View uses THAT instead, regardless of the session's
own mode. This is the entire mechanism BuildPalette (object.c) needs to
make the Palette behave like a permanent Clone station: it is a real
View, Mode="Clone", nothing else about it is special except Deletable.

*/

typedef struct InstanceData
{
	int active;
	int enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "View handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int View_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int View_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "View");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)View_Activate);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)View_OnEnable);

	SetPropStr(instance, "ReservedViewResizeable", "1");
	SetPropStr(instance, "ReservedViewMode", "");

	/* Open/PanelX/PanelY come from InitPosition like every other class - */
	/* a View's panel is not special, every thing's panel works the same   */
	InitPosition(instance);

	/* a view's panel needs room for contents - InitPosition's card-sized */
	/* W/H default (120x60) is too small for a container                   */
	SetPropInt(instance, "W", 190);
	SetPropInt(instance, "H", 220);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "View");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishProp(ClassSelf, "Enable",     "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",      "data", PROP_LED, "1");
	PublishProp(ClassSelf, "ReservedViewResizeable", "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "ReservedViewMode",       "data", PROP_TEXTBOX, "");
	PublishPosition(ClassSelf);

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;

	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "View");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c750");
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
