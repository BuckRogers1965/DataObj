
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Filter object: sits in the middle of a flow.

Its In port subscribes to a source, and whatever passes the test is
forwarded out its Out port to the filter's own subscribers.  Delivery
is queued through the scheduler (see SndMsg in object.c), so what
passes is copied into a fresh node rather than forwarding the one
this handler received - that one belongs to the queued delivery that
handed it to us, and will be freed once that delivery finishes.

The Mode property picks the test:

    "all"      pass everything (the default)
    "change"   pass only when the value differs from the last one seen
    "none"     pass nothing
    "ones"     pass only messages whose value is 1
    "zeros"    pass only messages whose value is 0

"change" is dedupe, chosen and visible, on the one wire that wants it. The
core used to do this to every property in the system - comparing a write
against the value already there and dropping it if they matched - which made
a repeated value unsendable everywhere. That is gone, so this is now the only
place the behaviour exists, and the only place it belongs.

msg_eof always passes, even through a disabled filter, so a stream can
always finish downstream.

A filter schedules no tasks and never holds the program open.

The Enable port: send a 1 to enable, a 0 to disable.  A disabled
filter drops data messages.  Enable is an ordinary input port, so any
source can drive it through Connect().

*/

enum { mode_all=0, mode_ones, mode_none, mode_zeros, mode_change };

typedef struct InstanceData
{
	int    active;
	int    enabled;
	int    mode;
	char * last;	/* last value seen, for mode_change */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem FilterPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Filter handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: test the message, forward it if it passes */
int Filter_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	//char * str;
	int pass;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");
	DebugPrint ( "Filter  1.", __FILE__, __LINE__, OBJMSGHANDLING);

	if (!local || !local->enabled)
		return rtrn_dropped;

	DebugPrint ( "Filter  2.", __FILE__, __LINE__, OBJMSGHANDLING);

	/* WHAT ARRIVED IS WHAT In HOLDS. The handler owns its property (see
	   DeliverToSubscriber's verdict rule), so if this object wants its own
	   In to show the traffic it says so here - nothing upstream does it on
	   our behalf. It is stored whether or not it passes: In is what came
	   in, which is exactly the question a reader of that readout is
	   asking. */
	if (message != msg_eof)
	{
		char *arrived = GetValueStr(data);

		SetPropStr(instance, "In", arrived ? arrived : "");
	}

	/* the end of the stream always passes so downstream can finish */
	if (message == msg_eof)
	{
		SndMsg(instance, "Out", msg_eof, NULL);
		return rtrn_handled;
	}

	DebugPrint ( "Filter  3.", __FILE__, __LINE__, OBJMSGHANDLING);

	//if (message != msg_send)
	//	return rtrn_dropped;

	DebugPrint ( "Filter  4.", __FILE__, __LINE__, OBJMSGHANDLING);

	//str = GetValueStr(data);
	//if (!str)
	//	return rtrn_dropped;

	char * mode;
	mode = GetPropStr(instance, "Mode");
	if (mode && strcmp(mode, "ones") == 0)
		local->mode = mode_ones;
	else if (mode && strcmp(mode, "zeros") == 0)
		local->mode = mode_zeros;
	else if (mode && strcmp(mode, "none") == 0)
		local->mode = mode_none;
	else if (mode && strcmp(mode, "change") == 0)
		local->mode = mode_change;
	else
		local->mode = mode_all;

	switch (local->mode)
	{

	case mode_ones:
		pass = (GetValueInt(data) != 0);
		break;

	case mode_zeros:
		pass = (GetValueInt(data) == 0);
		break;

	case mode_none:
		pass = 0;
		break;

	case mode_change:
	{
		/* the last value that PASSED, not the last one seen - a value
		   blocked by this filter never reached anyone, so it cannot be
		   what the far end is holding */
		char *now = GetValueStr(data);

		pass = (!local->last || !now || strcmp(local->last, now) != 0);
		if (pass)
		{
			if (local->last)
				free(local->last);
			local->last = now ? strdup(now) : NULL;
		}
		break;
	}

	default:
		pass = 1;
	}

	if (pass)
	{
		DebugPrint ( "Filter  5.", __FILE__, __LINE__, OBJMSGHANDLING);

		/* WHAT LEFT IS WHAT Out HOLDS, and one write does both jobs: the
		   property carries the value a reader (or a panel readout) can
		   see, and its fan-out is the delivery to everything subscribed.
		   Sending it as well would put the same value on the wire twice -
		   the property write and the message are not two mechanisms, they
		   are the same one. EOF still goes as a message: it has no value
		   to hold. */
		char *pass_value = GetValueStr(data);

		SetPropStr(instance, "Out", pass_value ? pass_value : "");
	}

	DebugPrint ( "Filter  6.", __FILE__, __LINE__, OBJMSGHANDLING);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Filter_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	//if (!local || message != msg_send)
//		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

int Filter_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	char * mode;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;


	if (local->active)
		return rtrn_handled;

	mode = GetPropStr(instance, "Mode");
	if (mode && strcmp(mode, "none") == 0)
		local->mode = mode_none;
	else if (mode && strcmp(mode, "change") == 0)
		local->mode = mode_change;
	else if (mode && strcmp(mode, "ones") == 0)
		local->mode = mode_ones;
	else if (mode && strcmp(mode, "zeros") == 0)
		local->mode = mode_zeros;
	else
		local->mode = mode_all;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. In and Out
   are the flow ports, each shown as a readout of the last message. */
static WidgetItem FilterPanel[] = {
	/* cls        prop     def   panel   x    y    w    h  label       [handler] */
	{ "View",     "Filter","",   0,   0,   0, 300, 260, 0 },			/* 0: main */
	{ "Help",     "objects/filter/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable","1",   0, 150, 12,   9,  9, LABEL_LEFT, (void *)Filter_OnEnable },
	{ "Dropdown", "Mode",  "all", 0,  15, 35, 120, 22, LABEL_NONE },
	{ "LED",      "State", "1",   0,  100, 35,  12, 12, LABEL_NONE },
	{ "TextOut",  "In",    "0",   0,  15,  80, 260, 20, LABEL_LEFT, (void *)Filter_OnIn },
	{ "TextOut",  "Out",   "0",   0,  15, 120, 260, 20, LABEL_LEFT },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->active = 0;
	local->enabled = 1;
	local->mode = mode_all;
	local->last = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Filter");

	/* every control's value + handler from the table (Enable/In carry a handler;
	   Mode/State/Out are plain data - In/Out are the ports, read on the panel) */
	Widget_Init(instance, FilterPanel);

    SetPropStr(instance, "ModeList", "all,change,none,ones,zeros");
	SetPropStr(instance, "ReservedIn",  "In");
	SetPropStr(instance, "ReservedOut", "Out");

	SetPropLong(instance, "local", (long)local);
	//SetPropLong(instance, "Activate", (long)Filter_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, FilterPanel);
	RegisterInstance(class, instance);

	/* placed where it was told, under the name it was given, panel and all */
	Widget_Place(instance, data, FilterPanel);
	/* NOT DeferBuild: Activate reads Mode exactly once (gated by
	   local->active, above) - an auto-activate one tick after creation
	   would read it before the client's own set-property Mode ever lands,
	   locking in the default "all" permanently (the later, real activate
	   is then a no-op). Same category as Pulse: activating this object
	   has real behavioral consequences, so it waits to be told. */

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->last)
			free(local->last);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Filter");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every control, from the table (In/Out among them, shown as readouts) */
	Widget_Publish(ClassSelf, FilterPanel);

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

	SetName(temp, "Filter");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c650");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "led.object", "LED", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
