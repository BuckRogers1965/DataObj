
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

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
	int     sent;		/* complete pulses sent so far this run */
	int     next;		/* the edge we send next, 1 or 0        */
	int     active;
	int     enabled;	/* the Enable port gates the ticking    */
	int     scheduled;	/* is the tick task currently armed?    */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem PulsePanel[];

/* Interval is read live, not snapshotted at Activate, so a running     */
/* pulse can be retuned from the UI - every place that arms the next    */
/* tick reads it fresh rather than trusting a value cached in the past  */
static int Pulse_CurrentInterval(NodeObj instance)
{
	int interval = GetPropInt(instance, "Interval");
	return (interval < 1) ? 1000 : interval;
}

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
	int count;

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

	if (local->next)
	{
		/* the falling edge is next - always send it before Count is  */
		/* ever allowed to end the train, so we never stop mid-pulse  */
		local->next = 0;
	}
	else
	{
		/* a full pulse is complete */
		local->next = 1;
		local->sent++;

		/* Count is read live too: 0 means forever, otherwise stop as */
		/* soon as sent reaches whatever Count currently says, even   */
		/* if it was changed after this train was Activated           */
		count = GetPropInt(instance, "Count");
		if (count > 0 && local->sent >= count)
		{
			/* finite train finished: EOF out the same port, go quiet */
			SndMsg(instance, "Out", msg_eof, NULL);
			local->active = 0;
			SetPropInt(instance, "State", Stopping);

			DebugPrint ( "Pulse finished its train, sent EOF, and deactivated.", __FILE__, __LINE__, OBJMSGHANDLING);
			return rtrn_handled;
		}
	}

	AddTaskMilli(local->task, Pulse_CurrentInterval(instance), (FuncPtr)Pulse_Tick, msg_send, instance);
	local->scheduled = 1;

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
				AddTaskMilli(local->task, Pulse_CurrentInterval(instance), (FuncPtr)Pulse_Tick, msg_send, instance);
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
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, PulsePanel);

	if (local->active)
		return rtrn_dropped;

	local->sent = 0;
	local->next = 1;
	/* one task struct for the instance's whole life, re-armed per run -   */
	/* a fresh CreateTask on every re-activation orphans the previous one  */
	/* (the leak counters caught exactly this, testharness/leaktest.py)    */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* first rising edge one interval from now */
	AddTaskMilli(local->task, Pulse_CurrentInterval(instance), (FuncPtr)Pulse_Tick, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. Out carries
   the "1"/"0" edge and is shown as an LED (like State), so you see it turn on
   and off. Uses the QUIET deferred build: placing a Pulse does NOT start it
   ticking - it stays quiet until a flow activates it. */
static WidgetItem PulsePanel[] = {
	/* cls        prop      def    panel   x    y    w   h  label       [handler] */
	{ "View",     "Pulse",  "",    0,   0,   0, 300, 230, 0 },			/* 0: main */
	{ "Help",     "objects/pulse/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "Checkbox", "Enable", "1",   0, 270,  12,   9,  9, LABEL_LEFT, (void *)Pulse_OnEnable },
	{ "Knob",     "Interval","1000",0, 15,  45,  60, 60, LABEL_NONE },
	{ "Textbox",  "Count",  "0",   0, 100,  55,  80, 22, LABEL_NONE },
	{ "LED",      "State",  "1",   0,  15, 135,  12, 12, LABEL_NONE },
	{ "LED",      "Out",    "0",   0,  70, 135,  12, 12, LABEL_NONE },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->task = NULL;
	local->sent = 0;
	local->next = 1;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Pulse");

	/* every control's value + handler from the table (Enable carries a handler;
	   Interval/Count/State/Out are plain data - Out is the edge, read on the panel) */
	Widget_Init(instance, PulsePanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Pulse_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, PulsePanel);
	RegisterInstance(class, instance);
	Widget_DeferBuildQuiet(instance, PulsePanel);	/* panel now, but do not start ticking */

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		/* stop the tick task before freeing local, or a still-scheduled */
		/* task fires later with a dangling instance pointer as its data */
		if (local->task)
			DeleteTask(local->task);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Pulse");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (Out shown as an LED edge, like State) */
	Widget_Publish(ClassSelf, PulsePanel);

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
