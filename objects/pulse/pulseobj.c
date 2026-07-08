
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Pulse object: a pure data source, no file, no socket, just the clock.

Sends "1" out its Out port, then "0" one Interval later, and repeats.
Interval is in milliseconds.  Count is the number of complete pulses
(a 1 followed by a 0) to send; 0 means pulse forever.

A finite pulse train ends like every other stream: after the last 0 it
sends msg_eof out the same port and stops rescheduling, so downstream
objects finish up and the system can quiesce.

Values travel as the strings "1" and "0"; sinks that want numbers get
them through the node data conversion (GetValueInt).

*/

typedef struct InstanceData
{
	TaskObj task;
	int     interval;	/* milliseconds between edges          */
	int     remaining;	/* complete pulses left, -1 = forever  */
	int     next;		/* the edge we send next, 1 or 0      */
	int     active;
	int     enabled;	/* the Enable port gates the ticking   */
	int     scheduled;	/* is the tick task currently armed?   */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Pulse handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* scheduler callback: send the next edge, reschedule until done */
int Pulse_Tick(NodeObj instance, NodeObj data, int reason)
{
	NodeObj edge;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* paused: hold our place in the train, the Enable port re-arms us */
	if (!local->enabled)
		return rtrn_handled;

	edge = NewNode(STRING);
	SetName(edge, "Data");
	SetValueStr(edge, local->next ? "1" : "0");
	SndMsg(instance, "Out", msg_send, edge);
	DelNode(edge);

	if (local->next)
	{
		/* the falling edge is next */
		local->next = 0;
	}
	else
	{
		/* a full pulse is complete */
		local->next = 1;
		if (local->remaining > 0)
			local->remaining--;
	}

	if (local->remaining == 0)
	{
		/* finite train finished: EOF out the same port, go quiet */
		SndMsg(instance, "Out", msg_eof, NULL);
		local->active = 0;
		SetPropInt(instance, "State", Stopping);

		DebugPrint ( "Pulse finished its train, sent EOF, and deactivated.", __FILE__, __LINE__, OBJMSGHANDLING);
	}
	else
	{
		AddTaskMilli(local->task, local->interval, (FuncPtr)Pulse_Tick, msg_send, instance);
		local->scheduled = 1;
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Pulse_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	if (GetValueInt(data))
	{
		if (!local->enabled)
		{
			local->enabled = 1;
			SetValueStr(GetPropNode(instance, "Enable"), "1");

			/* wake a paused train back up */
			if (local->active && !local->scheduled)
			{
				AddTaskMilli(local->task, local->interval, (FuncPtr)Pulse_Tick, msg_send, instance);
				local->scheduled = 1;
			}
		}
	}
	else
	{
		local->enabled = 0;
		SetValueStr(GetPropNode(instance, "Enable"), "0");
	}

	return rtrn_handled;
}

int Pulse_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	int count;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->interval = GetPropInt(instance, "Interval");
	if (local->interval < 1)
		local->interval = 1000;

	count = GetPropInt(instance, "Count");
	local->remaining = (count > 0) ? count : -1;

	local->next = 1;
	local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* first rising edge one interval from now */
	AddTaskMilli(local->task, local->interval, (FuncPtr)Pulse_Tick, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	NodeObj port;

	local->task = NULL;
	local->interval = 1000;
	local->remaining = -1;
	local->next = 1;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Pulse");
	SetPropInt(instance, "Interval", 1000);
	SetPropInt(instance, "Count", 0);
	SetPropInt(instance, "Out", 0);		/* output port, subscribers attach here */
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Pulse_Activate);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Pulse_OnEnable);

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

	SetName(class, "Pulse");
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

	SetName(temp, "Pulse");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "2b37c4c7-54d9-47d6-95e5-2dbffa208fa3");
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
