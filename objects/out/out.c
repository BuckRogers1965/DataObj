
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Out object: a debug probe.

Subscribe its In port to any source port and it prints every message
that flows past to standard out, tagged with its Label property, the
message id, and the payload size.

It prints synchronously in its handler and never schedules a task, so
a probe can be dropped onto any connection without holding the program
open or changing when the system quiesces.

The Echo property (default on) can be set to 0 to silence a probe
without disconnecting it.

*/

typedef struct InstanceData
{
	int active;
	int enabled;		/* the Enable port gates the printing        */
	unsigned long messages;	/* how many messages have passed this probe */
	unsigned long bytes;	/* how many payload bytes have passed        */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem OutPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Out probe handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: print whatever flows past, then let it go */
int Out_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * label;
	char * str;
	size_t length;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	if (!local->enabled)
		return rtrn_propagate;

	if (!GetPropInt(instance, "Echo"))
		return rtrn_propagate;

	label = GetPropStr(instance, "Label");
	if (!label || !label[0])
		label = "probe";

	switch (message)
	{
	case msg_send:
		str = GetValueStr(data);
		length = str ? strlen(str) : 0;
		local->messages++;
		local->bytes += length;

		printf("[%s] msg_send %lu bytes: %s\n", label, (unsigned long)length, str ? str : "");
		break;

	case msg_eof:
		printf("[%s] msg_eof after %lu messages, %lu bytes\n", label, local->messages, local->bytes);
		break;

	default:
		printf("[%s] message id %d\n", label, message);
		break;
	}

	fflush(stdout);

	/* a probe only watches, the message is not ours to consume */
	return rtrn_propagate;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Out_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

int Out_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, OutPanel);

	if (local->active)
		return rtrn_handled;

	local->active = 1;
	local->messages = 0;
	local->bytes = 0;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. In is the
   probe's watched input, shown as a readout of the last message that passed. */
static WidgetItem OutPanel[] = {
	/* cls        prop     def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Out",   "",  0,   0,   0, 280, 240, 0 },			/* 0: main */
	{ "Help",     "objects/out/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable","1", 0, 250,  12,   9,  9, LABEL_LEFT, (void *)Out_OnEnable },
	{ "Textbox",  "Label", "",  0,  15,  35, 240, 22, LABEL_NONE },
	{ "Checkbox", "Echo",  "1", 0,  15,  78,   9,  9, LABEL_NONE },
	{ "LED",      "State", "1", 0,  15, 108,  12, 12, LABEL_NONE },
	{ "TextOut",  "In",    "",  0,  15, 150, 240, 20, LABEL_LEFT, (void *)Out_OnIn },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->active = 0;
	local->enabled = 1;
	local->messages = 0;
	local->bytes = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Out");

	/* every control's value + handler from the table (Enable carries a handler;
	   Label/Echo/State are plain data) */
	Widget_Init(instance, OutPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Out_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, OutPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, OutPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Out");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table - In among them, shown as a readout */
	Widget_Publish(ClassSelf, OutPanel);

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

	SetName(temp, "Out");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c600");
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
