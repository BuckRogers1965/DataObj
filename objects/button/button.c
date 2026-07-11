
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Button object: the other half of "widgets have to be real objects" -
distinct from objects/widget/widget.c because a button's whole point is
momentary action, not a value that sits and reflects state. Its Out
port fires a message every time Activate runs, and unlike almost every
other object in this framework, Activate here is NOT one-shot: a
button that could only ever be pressed once would not be a button.
State still tracks Starting/Running so a rendered button can show it's
live, but "active" never latches a press out.

Wire it as: ConnectToActivate(Button1, "Out", target) - the adapter
in object.c that lets a Button's press reach an arbitrary target's
ActivateInstance, the same way ConnectToProperty lets an input widget's
Value reach an arbitrary target property. Pressing the rendered button
sends {"cmd":"activate","instance":"Button1"} - activating the BUTTON,
which is what actually fires the Out message the adapter is listening
for; the target's own activation is a consequence of that wiring, not
something the client asks for directly.

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
	DebugPrint ( "Button handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Button_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* deliberately not one-shot: every call is a fresh press */
int Button_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj chunk;

	if (!local || !local->enabled)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	chunk = NewNode(STRING);
	SetName(chunk, "Press");
	SetValueStr(chunk, "1");
	SndMsg(instance, "Out", msg_send, chunk);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Button");
	SetPropStr(instance, "Label", "");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropInt(instance, "Out", 0);		/* fires once per press */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Button_Activate);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Button_OnEnable);

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

	SetName(class, "Button");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Label",  "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Enable", "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Out",    "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "State",  "data", PROP_LED, "1");

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

	SetName(temp, "Button");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c730");
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
