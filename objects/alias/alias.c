#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Alias object: a stand-in for one property of another instance - the
"copy a control out of a widget" primitive, and the seed of composite-
object ports.

The aliased property itself is a node-level link (LinkProperty,
object.c): its value, its subscribers, and anything wired to it all
live ONLY on the original. Connect/SndMsg/SetOrDeliverProp/the
Bridge's subscribe resolve through the link (ResolvePort), so twelve
aliases of one control are twelve zero-cost doorways to the same
node - there is no forwarding, no second state, nothing to keep in
sync, and this object needs no handlers or tasks of its own.

What the Alias does own is its presentation: Target/TargetProp say
what it stands for (set by the Bridge's create-alias, which also makes
the link), and Widget/Label/position are the alias's own - render the
same underlying value as a Knob here and a Textbox there without the
original's appearance changing.

If the original is deleted, DeleteInstance scrubs the link
(ScrubRegistryLinks, object.c) and the alias survives as a dead
control - same policy as scrubbed subscriptions.

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
	DebugPrint ( "Alias handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Alias_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int Alias_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	SetName(instance, "Alias");

	/* what this alias stands for - create-alias (bridge.c) fills these  */
	/* in and makes the actual link; they are ordinary watchable          */
	/* properties so any client can ask what it is looking at             */
	SetPropStr(instance, "Target", "");
	WatchableProp(instance, "Target");
	SetPropStr(instance, "TargetProp", "");
	WatchableProp(instance, "TargetProp");

	/* the alias's OWN presentation - restyling an alias never touches   */
	/* the original. Widget and Direction are stamped at birth by the     */
	/* engine (create-alias / internals, bridge.c) from what the target's  */
	/* class published; clients render them and deduce nothing.            */
	SetPropStr(instance, "Widget", "");
	WatchableProp(instance, "Widget");
	SetPropStr(instance, "Direction", "");
	WatchableProp(instance, "Direction");
	SetPropStr(instance, "Label", "");
	WatchableProp(instance, "Label");

	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Alias_Activate);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Alias_OnEnable);

	InitPosition(instance);

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

	SetName(class, "Alias");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Target",     "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "TargetProp", "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Widget",     "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Direction",  "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Label",      "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Enable",     "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",      "data", PROP_LED, "1");

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

	SetName(temp, "Alias");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "59c559c8-de8a-4095-897c-1b712cad8f77");
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
