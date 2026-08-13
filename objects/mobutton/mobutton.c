
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

MoButton object: the MOMENTARY button. Distinct from the two controls that
already exist:

	Button    fires once - an Activate trigger
	Checkbox  latches - it holds the state you left it in
	MoButton  is held: pressing sends one edge, releasing sends the other

Pressing sends "1" out Out, releasing sends "0" - which is exactly the
Pulse's rising-then-falling edge convention, so a MoButton is a hand-driven
Pulse and every sink downstream already knows what to do with it. Wire it at
an Enable to hold something on while pressed, at a command port (a TCPPort's
Send, say) to invoke it, at a Queue's Clock to step it by hand.

AutoRepeat re-sends the "1" every Interval milliseconds while the button is
held, for scroll/jog behavior; 0 means no repeat, which is the default.

Value carries the current 1/0 while held, so the button's own state is
readable and subscribable like anything else. The "only fire the release
action if the mouse comes up INSIDE the control" rule is presentation - it
belongs with the projector, which is where the press/release gestures live.

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
/* the button's state IS the thing it reports - writing Value fans out to
   whatever subscribed to it. There is no second "Out" to keep in step. */
static void MoButton_Edge(NodeObj instance, char *level)
{
	SetPropStr(instance, "Value", level);
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
	{
		char dbg[300];
		snprintf(dbg, sizeof(dbg), "MoButton_OnPress dropped on '%s': local=%p enabled=%d message=%d",
				 GetPropStr(instance, "Name"), (void *)local, local ? local->enabled : -1, message);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
		return rtrn_dropped;
	}

	down = GetValueInt(data) ? 1 : 0;
	if (down == local->down)
		return rtrn_handled;		/* no edge, no message */

	local->down = down;
	{
		char dbg[200];
		snprintf(dbg, sizeof(dbg), "MoButton_OnPress Value=%s on '%s'", down ? "1" : "0", GetPropStr(instance, "Name"));
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}
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
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)MoButton_Activate);

	/* Value IS the button: 1 while held, 0 when released. Writing it is
	   how the hand (or anything wired in) presses it, and the same write
	   fans out to whatever is listening. One property, both directions. */
	port = GetPropNode(instance, "Value");
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

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Control");

	PublishPosition(ClassSelf);

	/* how it shows itself, carried by the class - see show/web/ */
	PublishShow(ClassSelf, 0, show_web_js, show_web_css);

	PublishProp(ClassSelf, "Label", PROP_TEXTBOX, "Press");
	PublishProp(ClassSelf, "Value", PROP_LED, "0");
	PublishProp(ClassSelf, "Interval", PROP_TEXTBOX, "0");
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

	SetName(temp, "MoButton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b6f04a19-7c25-4d83-9e61-30af5d8c2b47");
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
