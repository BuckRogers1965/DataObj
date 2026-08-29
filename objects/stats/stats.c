#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "buff.h"
#include "queue.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Stats object: the core's allocation counters, published as ordinary
properties so the fabric itself becomes the leak detector.

The core counts what it allocates (NewNode/DelNode, NewData/DelData,
SndMsg envelopes, task_entry structs, buffs, queues - plain statics
behind getter functions; counting is mechanism and lives in the core,
publishing is behavior and lives here, the Phase 8 split). A Stats
instance samples those getters on a timer and writes any CHANGED value
into its Nodes/Datas/Envelopes/Tasks/Buffs/Queues properties - which
fan out to subscribers like every property write does, so a TextOut
wired to Nodes is a live leak readout on the canvas, a Filter in
"change" mode feeds a Writer to log allocation history, a probe taps
it like anything else.

The leak discipline these counters serve: run a create/destroy cycle
and compare - a counter that grows and never shrinks IS a leak, named
by its type (testharness/leaktest.py drives exactly that through the
raw protocol).

Writes happen only when a value actually changed, so an idle system's
Stats instance is itself quiet - no allocation churn from watching the
allocator.

*/

typedef struct InstanceData
{
	TaskObj task;
	int     active;
	int     enabled;
	int     scheduled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem StatsPanel[];

static int Stats_CurrentInterval(NodeObj instance)
{
	int interval = GetPropInt(instance, "Interval");
	return (interval < 100) ? 1000 : interval;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Stats handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* write one sampled counter into its property, only on change - the    */
/* write itself fans out to subscribers (node.c), that is the whole      */
/* publication mechanism                                                  */
static void Stats_Publish(NodeObj instance, char * propname, long value)
{
	char buf[24];
	char * old;

	snprintf(buf, sizeof(buf), "%ld", value);

	old = GetPropStr(instance, propname);
	if (old && strcmp(old, buf) == 0)
		return;

	SetPropStr(instance, propname, buf);
}

/* scheduler callback: sample every counter, publish changes, re-arm */
int Stats_Tick(NodeObj instance, NodeObj data, int reason)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* paused: stop sampling, the Enable port re-arms us */
	if (!local->enabled)
		return rtrn_handled;

	{
		/* sample EVERYTHING first, then publish: publishing is itself     */
		/* allocation (event chunks, queued envelopes), so interleaving    */
		/* read-and-write makes the observer watch its own wake forever -  */
		/* Envelopes would oscillate 0 <-> k on an otherwise idle system   */
		long nodes     = NodeCount();
		long datas     = DataCount();
		long envelopes = EnvelopeCount();
		long tasks     = TaskStructCount();
		long buffs     = BuffCount();
		long queues    = QueueCount();

		Stats_Publish(instance, "Nodes",     nodes);
		Stats_Publish(instance, "Datas",     datas);
		Stats_Publish(instance, "Envelopes", envelopes);
		Stats_Publish(instance, "Tasks",     tasks);
		Stats_Publish(instance, "Buffs",     buffs);
		Stats_Publish(instance, "Queues",    queues);
	}

	AddTaskMilli(local->task, Stats_CurrentInterval(instance), (FuncPtr)Stats_Tick, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Stats_OnEnable(NodeObj instance, MsgId message, NodeObj data)
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

			if (local->active && !local->scheduled)
			{
				AddTaskMilli(local->task, Stats_CurrentInterval(instance), (FuncPtr)Stats_Tick, msg_send, instance);
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

/* START SAMPLING. Called from InstanceStart, which IS the init message
   (object.c calls it as InstanceStart(class, msg_initialize, place)) -
   there is no separate verb that turns a widget on, and waiting for one
   is why every readout in this panel sat at its default: Enable showing
   1, Interval showing 1000, State showing 1, and six counters showing
   the empty string they were built with, which a TextOut paints as
   nothing at all. */
static void Stats_Start(NodeObj instance)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return;

	/* one task struct for the instance's whole life - see leaktest.py */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* first sample right away - one tick from now would leave the      */
	/* readouts blank for a whole interval                              */
	AddTaskMilli(local->task, 1, (FuncPtr)Stats_Tick, msg_send, instance);
	local->scheduled = 1;
}

/* The whole panel in one table: main view, Help, and every control - six live
   counters, the sampling-interval knob, the enable, and the state LED. Stats
   samples the core's alloc counters; the readouts ARE its outputs. */
static WidgetItem StatsPanel[] = {
	/* cls        prop        def    panel   x    y    w   h  label       [handler] */
	{ "View",     "Stats",    "",    0,   0,   0, 320, 320, 0 },			/* 0: main */
	{ "Help",     "objects/stats/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	/* over the right-hand column, ending where Datas ends (175 + 120) -
	   never out at the panel's rim */
	{ "Checkbox", "Enable",   "1",   0, 241,  12,   9,  9, LABEL_LEFT, (void *)Stats_OnEnable },
	/* LABELLED, to the left of each number: unlabelled readouts say nothing
	   about what they are counting, and an empty one paints nothing at all.
	   Left, not top - a top label stacks into the control below it. */
	{ "TextOut",  "Nodes",     "",   0,  15,  40, 120, 20, LABEL_LEFT },
	{ "TextOut",  "Datas",     "",   0, 175,  40, 120, 20, LABEL_LEFT },
	{ "TextOut",  "Envelopes", "",   0,  15,  85, 120, 20, LABEL_LEFT },
	{ "TextOut",  "Tasks",     "",   0, 175,  85, 120, 20, LABEL_LEFT },
	{ "TextOut",  "Buffs",     "",   0,  15, 130, 120, 20, LABEL_LEFT },
	{ "TextOut",  "Queues",    "",   0, 175, 130, 120, 20, LABEL_LEFT },
	{ "Knob",     "Interval", "1000",0,  15, 185,  60, 60, LABEL_LEFT },
	{ "LED",      "State",    "1",   0, 130, 200,  12, 12, LABEL_LEFT },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->task = NULL;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Stats");

	/* every control's value + handler from the table (Enable carries a handler;
	   the six counters, Interval and State are plain data) */
	Widget_Init(instance, StatsPanel);

	SetPropLong(instance, "local", (long)local);

	InitPosition(instance);
	Widget_MainSize(instance, StatsPanel);
	RegisterInstance(class, instance);

	/* placed where it was told, under the name it was given, panel and all */
	Widget_Place(instance, data, StatsPanel);

	/* and it samples from here, because this call is the init message */
	Stats_Start(instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		/* stop the sampling task before freeing local, or a scheduled  */
		/* tick fires later with a dangling instance pointer as data    */
		if (local->task)
			DeleteTask(local->task);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Stats");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every control, from the table (the six counters are the readouts) */
	Widget_Publish(ClassSelf, StatsPanel);

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

	SetName(temp, "Stats");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8f3a1c2e-9b47-4d05-a6e8-51c7d90b3f14");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "knob.object", "Knob", "1", "0");
	AddDependency(temp, "led.object", "LED", "1", "0");
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
