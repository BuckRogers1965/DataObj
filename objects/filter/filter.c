
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Filter object: sits in the middle of a flow.

Its In port subscribes to a source, and whatever passes the test is
forwarded out its Out port to the filter's own subscribers.  Delivery
is synchronous all the way down, so the same data node is forwarded
without copying and a chain of filters costs a few function calls.

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
		SndMsg(instance, "Out", msg_send, data);

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

	if (!local || local->active)
		return rtrn_dropped;

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

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;
	local->mode = mode_all;
	local->last = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Filter");
	SetPropStr(instance, "Mode", "all");
	SetPropInt(instance, "State", Starting);
	SetPropInt(instance, "Out", 0);		/* output port, subscribers attach here */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Filter_Activate);

	/* input port: Connect() reads the OnMsg handler off this port */
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Filter_OnIn);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Filter_OnEnable);

	RegisterInstance(class, instance);

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
