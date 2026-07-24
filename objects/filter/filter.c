
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
    "ones"     pass only messages whose value is 1
    "zeros"    pass only messages whose value is 0

msg_eof always passes, even through a disabled filter, so a stream can
always finish downstream.

A filter schedules no tasks and never holds the program open.

The Enable port: send a 1 to enable, a 0 to disable.  A disabled
filter drops data messages.  Enable is an ordinary input port, so any
source can drive it through Connect().

*/

enum { mode_all=0, mode_change, mode_ones, mode_zeros };

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
	char * str;
	int pass;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	/* the end of the stream always passes so downstream can finish */
	if (message == msg_eof)
	{
		SndMsg(instance, "Out", msg_eof, NULL);
		return rtrn_handled;
	}

	if (message != msg_send)
		return rtrn_dropped;

	if (!local->enabled)
		return rtrn_dropped;

	str = GetValueStr(data);
	if (!str)
		return rtrn_dropped;

	switch (local->mode)
	{
	case mode_change:
		pass = (!local->last || strcmp(local->last, str) != 0);
		if (local->last)
			free(local->last);
		local->last = strdup(str);
		break;

	case mode_ones:
		pass = (GetValueInt(data) == 1);
		break;

	case mode_zeros:
		pass = (strcmp(str, "0") == 0);
		break;

	default:
		pass = 1;
	}

	if (pass)
	{
		/* SndMsg now queues delivery and takes ownership of what it's  */
		/* given, freeing it once sent - data here belongs to whatever  */
		/* queued send delivered it to us, so it must be copied rather  */
		/* than forwarded, or two independent deliveries would each     */
		/* try to free the same node                                    */
		NodeObj forward = NewNode(STRING);
		SetName(forward, "Data");
		SetValueStr(forward, str);
		SndMsg(instance, "Out", msg_send, forward);
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Filter_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

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

	Widget_BuildOnce(instance, FilterPanel);

	if (local->active)
		return rtrn_handled;

	mode = GetPropStr(instance, "Mode");
	if (mode && strcmp(mode, "change") == 0)
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
	{ "View",     "Filter","",   0,   0,   0, 300, 245, 0 },			/* 0: main */
	{ "Help",     "objects/filter/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable","1",  0, 270,  12,   9,  9, LABEL_LEFT, (void *)Filter_OnEnable },
	{ "Textbox",  "Mode",  "all",0,  15,  35, 120, 22, LABEL_NONE },
	{ "LED",      "State", "1",  0,  15,  78,  12, 12, LABEL_NONE },
	{ "TextOut",  "In",    "",   0,  15, 118, 260, 20, LABEL_LEFT, (void *)Filter_OnIn },
	{ "TextOut",  "Out",   "",   0,  15, 160, 260, 20, LABEL_LEFT },

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

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Filter_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, FilterPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, FilterPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
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
