
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "control.h"
#include "show_web.h"

/*

Knob object: a genuinely standalone class, pulled out of the old
objects/widget/widget.c grab-bag the same way LED and Button already
were. See objects/led/led.c's doc comment for the reasoning.

A pure display/input sink: no task, nothing to schedule. Whatever
arrives on In becomes the displayed Value - Connect(SomeSource,
"SomeProp", Knob1, "In") is enough, since every property fans out
to whatever's Connect()ed to it (WatchableProp, object.c) the same way
a real Out port does. There is deliberately no Out port: reaching back
out to an arbitrary target property is ConnectToProperty's job
(object.c), not this object's.

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
	DebugPrint ( "Knob handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: whatever arrives becomes the displayed Value - */
/* no message-type filter, deliberately: a watchable property's own fan- */
/* out (PropertyChanged) arrives as msg_change, a real port's as msg_send */
int Knob_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled)
		return rtrn_dropped;

	value = GetValueStr(data);
	SetPropStr(instance, "Value", value ? value : "");

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Knob_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int Knob_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	SetName(instance, "Knob");
	SetPropStr(instance, "Value", "0");
	WatchableProp(instance, "Value");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Knob_Activate);

	/* Value IS the control: a wire into it writes it, the hand writes
	   it, and the write fans out. There is no second copy to keep in
	   step and nothing to deliver that can overwrite it. */
	port = GetPropNode(instance, "Value");
	SetPropLong(port, "OnMsg", (long)Knob_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Knob_OnEnable);


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

	SetName(class, "Knob");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Control");

	PublishPosition(ClassSelf);

	/* how it shows itself, carried by the class - see show/web/ */
	PublishShow(ClassSelf, PROP_KNOB, show_web_js, show_web_css);

	PublishProp(ClassSelf, "Value", PROP_KNOB, "0");
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State", PROP_LED, "1");

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

	SetName(temp, "Knob");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c744");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "control.object", "Control", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
