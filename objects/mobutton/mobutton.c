
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

MoButton object: the MOMENTARY button, ported from the VNOS drop-in
control of the same name (objects/demo/mobutton/mobutton.c - "two-state
button"). Distinct from the two controls that already exist:

	Button    fires once - an Activate trigger
	Checkbox  latches - it holds the state you left it in
	MoButton  is held: pressing sends one edge, releasing sends the other

Pressing sends "1" out Out, releasing sends "0" - which is exactly the
Pulse's rising-then-falling edge convention, so a MoButton is a
hand-driven Pulse and every sink downstream already knows what to do
with it. Wire it at an Enable to hold something on while pressed, at a
command port (a TCPPort's Send, say) to invoke it, at a Queue's Clock
to step it by hand.

AutoRepeat (the VNOS AUTO_TRACK variant) re-sends the "1" every
Interval milliseconds while the button is held, for scroll/jog
behavior; 0 means no repeat, which is the default.

Value carries the current 1/0 while held, so the button's own state is
readable and subscribable like anything else. The original's "only
fire the release action if the mouse comes up INSIDE the control" rule
is presentation - it belongs with the projector, which is where the
press/release gestures live.

*/

typedef struct InstanceData
{
	int      active;
	int      enabled;
	int      down;			/* is the button currently held?        */
	TaskObj  repeat;		/* the auto-repeat task, armed on press */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "MoButton handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* one edge out Out - the same shape Pulse emits, so sinks need no        */
/* special knowledge that a hand rather than a timer made it              */
static void MoButton_Edge(NodeObj instance, char *level)
{
	NodeObj out = NewNode(STRING);

	SetName(out, "Data");
	SetValueStr(out, level);
	SndMsg(instance, "Out", msg_send, out);
}

/* auto-repeat while held: re-send the "1" every Interval ms. Armed on    */
/* press, never re-created per fire (one task per instance life - the     */
/* re-activation leak lesson), and simply not re-armed once released.     */
int MoButton_Repeat(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int interval;

	if (!local || !local->down || !local->enabled)
		return rtrn_handled;

	MoButton_Edge(instance, "1");

	interval = GetPropInt(instance, "Interval");
	if (interval > 0)
		AddTaskMilli(local->repeat, interval, (FuncPtr)MoButton_Repeat, msg_send, instance);

	return rtrn_handled;
}

/* the press/release gesture arrives here: 1 is down, 0 is up. It is an   */
/* ordinary in port, so a script or a Pulse can "press" this button       */
/* exactly as a finger does - nothing about the hand is privileged.       */
int MoButton_OnPress(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int down, interval;

	if (!local || !local->enabled || message == msg_eof)
		return rtrn_dropped;

	down = GetValueInt(data) ? 1 : 0;
	if (down == local->down)
		return rtrn_handled;		/* no edge, no message */

	local->down = down;
	SetPropStr(instance, "Value", down ? "1" : "0");
	MoButton_Edge(instance, down ? "1" : "0");

	interval = GetPropInt(instance, "Interval");
	if (down && interval > 0 && local->repeat)
		AddTaskMilli(local->repeat, interval, (FuncPtr)MoButton_Repeat, msg_send, instance);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int MoButton_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	/* a button disabled mid-press releases - never leave a sink latched   */
	/* on because the button stopped listening while held                  */
	if (!local->enabled && local->down)
	{
		local->down = 0;
		SetPropStr(instance, "Value", "0");
		MoButton_Edge(instance, "0");
	}

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int MoButton_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	local->down = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "MoButton");

	SetPropStr(instance, "Label", "Press");
	SetPropStr(instance, "Value", "0");
	SetPropInt(instance, "Interval", 0);	/* 0 = no auto-repeat */
	SetPropInt(instance, "Out", 0);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)MoButton_Activate);

	/* the gesture port: the projector sends 1 on press, 0 on release -    */
	/* and so can anything else wired to it                                 */
	SetPropStr(instance, "Press", "0");
	port = GetPropNode(instance, "Press");
	SetPropLong(port, "OnMsg", (long)MoButton_OnPress);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)MoButton_OnEnable);

	/* created once per instance life, armed and re-armed on press only    */
	/* (a fresh CreateTask per press would orphan the previous one)         */
	local->repeat = CreateTask(ObjGetTaskList());

	InitPosition(instance);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		/* stop the repeat task before freeing local, or a still-scheduled */
		/* task fires later with a dangling instance pointer as its data    */
		if (local->repeat)
			DeleteTask(local->repeat);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "MoButton");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Label",    "data", PROP_TEXTBOX, "Press");
	PublishProp(ClassSelf, "Value",    "data", PROP_LED, "0");
	PublishProp(ClassSelf, "Interval", "data", PROP_TEXTBOX, "0");
	PublishProp(ClassSelf, "Press",    "in",   PROP_NULL, "0");
	PublishProp(ClassSelf, "Out",      "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Enable",   "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",    "data", PROP_LED, "1");

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

	SetName(temp, "MoButton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b6f04a19-7c25-4d83-9e61-30af5d8c2b47");
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
