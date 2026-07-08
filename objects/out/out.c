
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

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

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	local->messages = 0;
	local->bytes = 0;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, inPort;
	InstanceData * local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;
	local->messages = 0;
	local->bytes = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Out");
	SetPropStr(instance, "Label", "");
	SetPropInt(instance, "Echo", 1);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Out_Activate);

	/* input port: Connect() reads the OnMsg handler off this port */
	SetPropInt(instance, "In", 0);
	inPort = GetPropNode(instance, "In");
	SetPropLong(inPort, "OnMsg", (long)Out_OnIn);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	inPort = GetPropNode(instance, "Enable");
	SetPropLong(inPort, "OnMsg", (long)Out_OnEnable);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

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
