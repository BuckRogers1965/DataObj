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

int Stats_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, StatsPanel);

	if (local->active)
		return rtrn_handled;

	/* one task struct for the instance's whole life - see leaktest.py */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* first sample right away - one tick from now would leave the      */
	/* readouts blank for a whole interval after activation             */
	AddTaskMilli(local->task, 1, (FuncPtr)Stats_Tick, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control - six live
   counters, the sampling-interval knob, the enable, and the state LED. Stats
   samples the core's alloc counters; the readouts ARE its outputs. */
static WidgetItem StatsPanel[] = {
	/* cls        prop        def    panel   x    y    w   h  label       [handler] */
	{ "View",     "Stats",    "",    0,   0,   0, 320, 290, 0 },			/* 0: main */
	{ "Help",     "objects/stats/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "Checkbox", "Enable",   "1",   0, 290,  12,   9,  9, LABEL_LEFT, (void *)Stats_OnEnable },
	{ "TextOut",  "Nodes",     "",   0,  15,  40, 120, 20, LABEL_NONE },
	{ "TextOut",  "Datas",     "",   0, 175,  40, 120, 20, LABEL_NONE },
	{ "TextOut",  "Envelopes", "",   0,  15,  85, 120, 20, LABEL_NONE },
	{ "TextOut",  "Tasks",     "",   0, 175,  85, 120, 20, LABEL_NONE },
	{ "TextOut",  "Buffs",     "",   0,  15, 130, 120, 20, LABEL_NONE },
	{ "TextOut",  "Queues",    "",   0, 175, 130, 120, 20, LABEL_NONE },
	{ "Knob",     "Interval", "1000",0,  15, 185,  60, 60, LABEL_NONE },
	{ "LED",      "State",    "1",   0, 130, 200,  12, 12, LABEL_NONE },

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
	SetPropLong(instance, "Activate", (long)Stats_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, StatsPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, StatsPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
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
