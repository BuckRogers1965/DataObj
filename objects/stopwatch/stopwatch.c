
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "timer.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Stopwatch object: a stop-watch instrument panel. It measures and reports the
time between a start event and a stop event.

Wire a PulseGenerator's Out to this Run and it TIMES THE PULSES: with Run
Edge = Positive and Stop Edge = Run Ends, timing starts on the rising edge
and stops on the falling edge, so Duration reads the pulse's high width.
(Point Stop Edge at a separate Stop line to time between two different events
instead.)

Everything here is a property. Run/Stop/Enable carry a handler so a write
acts; there is no "in"/"out" port. A view exposes its external inputs/outputs
as aliases onto these properties.

Dataflow: the default input connection is to Run, the default output
connection is from Duration.

*/

/* NO hardcoded help: the help lives in objects/stopwatch/README.md and is
   read from disk into the Help box when the panel is opened. */

typedef struct InstanceData
{
	int           enabled;		/* the Enable checkbox - gates timing        */
	int           active;		/* is a measurement in progress?             */
	unsigned long startSec;		/* the cached clock at the start event       */
	unsigned long startUsec;
	long          lastMs;		/* last measured interval, ms; -1 = none yet */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem StopwatchPanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Stopwatch handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- timing --------------------------------------------------------- */

static int Stopwatch_Is(NodeObj instance, char *prop, char *val)
{
	char *cur = GetPropStr(instance, prop);
	return cur && strcmp(cur, val) == 0;
}

/* render the last measurement into Duration, in the chosen Time Scale.
   Duration is just a property, so the
   readout wired to it updates when it changes. */
static void Stopwatch_ShowDuration(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char buf[64];

	if (!local || local->lastMs < 0)
	{
		SetPropStr(instance, "Duration", "");
		return;
	}

	if (Stopwatch_Is(instance, "TimeUnits", "secs"))
		snprintf(buf, sizeof(buf), "%.3f", local->lastMs / 1000.0);
	else
		snprintf(buf, sizeof(buf), "%ld", local->lastMs);

	SetPropStr(instance, "Duration", buf);
}

static void Stopwatch_Start(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->enabled || local->active)
		return;

	GetCurrentTime(&local->startSec, &local->startUsec);
	local->active = 1;
	SetPropStr(instance, "OnEvent", "1");
	SetPropStr(instance, "OffEvent", "0");
}

/* close the interval and publish it */
static void Stopwatch_StopTimer(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	unsigned long endSec, endUsec;

	if (!local || !local->active)
		return;

	GetCurrentTime(&endSec, &endUsec);
	local->lastMs = (long)(endSec - local->startSec) * 1000
				  + ((long)endUsec - (long)local->startUsec) / 1000;
	if (local->lastMs < 0)
		local->lastMs = 0;

	local->active = 0;
	SetPropStr(instance, "OnEvent", "0");
	SetPropStr(instance, "OffEvent", "1");
	Stopwatch_ShowDuration(instance);
}

/* ---- action handlers ------------------------------------------------ */

/* Run: a write of 1 (press) or 0 (release). The Run Edge decides which of
   those STARTS timing; the other edge, when Stop Edge is "Run Ends",
   stops it. */
int Stopwatch_OnRun(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int v;

	if (!local || message == msg_eof)
		return rtrn_handled;

	v = GetValueInt(data) ? 1 : 0;

	/* the start edge: Positive starts on 1, Negative on 0 */
	if ((v == 1 && Stopwatch_Is(instance, "RunEdge", "Positive"))
		|| (v == 0 && Stopwatch_Is(instance, "RunEdge", "Negative")))
		Stopwatch_Start(instance);

	/* the opposite edge ends the run when Stop Edge is "Run Ends" */
	if ((v == 0 && Stopwatch_Is(instance, "RunEdge", "Positive"))
		|| (v == 1 && Stopwatch_Is(instance, "RunEdge", "Negative")))
	{
		if (Stopwatch_Is(instance, "StopEdge", "Run Ends"))
			Stopwatch_StopTimer(instance);
	}

	return rtrn_handled;
}

/* Stop: Positive stops on 1, Negative on 0 */
int Stopwatch_OnStop(NodeObj instance, MsgId message, NodeObj data)
{
	int v;

	if (message == msg_eof)
		return rtrn_handled;

	v = GetValueInt(data) ? 1 : 0;

	if ((v == 1 && Stopwatch_Is(instance, "StopEdge", "Positive"))
		|| (v == 0 && Stopwatch_Is(instance, "StopEdge", "Negative")))
		Stopwatch_StopTimer(instance);

	return rtrn_handled;
}

/* Enable: 0 stops any running measurement and gates new ones */
int Stopwatch_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	if (!local->enabled)
		Stopwatch_StopTimer(instance);

	return rtrn_handled;
}

/* Time Scale changed: re-display the same measurement in the new unit. */
int Stopwatch_OnTimeUnits(NodeObj instance, MsgId message, NodeObj data)
{
	char *pick;

	if (message == msg_eof)
		return rtrn_handled;

	pick = data ? GetValueStr(data) : NULL;
	if (pick)
		SetValueStr(GetPropNode(instance, "TimeUnits"), pick);

	Stopwatch_ShowDuration(instance);
	return rtrn_handled;
}

/* Placement setup: settle the LEDs
   (Off lit, On dark) and clear any in-progress state. */
int Stopwatch_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, StopwatchPanel);

	local->active = 0;
	SetPropStr(instance, "OnEvent", "0");
	SetPropStr(instance, "OffEvent", "1");

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->active = 0;
	local->startSec = 0;
	local->startUsec = 0;
	local->lastMs = -1;

	instance = NewNode(INTEGER);
	SetName(instance, "Stopwatch");

	/* every control's value + handler from the table: Run/Stop/Enable/TimeUnits
	   carry a handler, the rest are plain data */
	Widget_Init(instance, StopwatchPanel);

	/* the dropdowns' backing option lists and the lifecycle state - no control */
	SetPropStr(instance, "RunEdgeList", "Positive,Negative");
	SetPropStr(instance, "StopEdgeList", "Run Ends,Positive,Negative");
	SetPropStr(instance, "TimeUnitsList", "msecs,secs");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Stopwatch_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, StopwatchPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, StopwatchPanel);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, and every control (value +,
   for the ports, handler). Backing *List props and State are published apart. */
static WidgetItem StopwatchPanel[] = {
	/* cls        prop         def         panel   x    y    w   h  label       [handler] */
	{ "View",     "Stopwatch", "",         0,   0,   0, 300, 250, 0 },			/* 0: main */
	{ "Help",     "objects/stopwatch/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "Checkbox", "Enable",    "1",        0, 205,  13,  9,  9, LABEL_LEFT, (void *)Stopwatch_OnEnable },
	{ "MoButton", "Run",       "0",        0,  21,  52, 40, 20, LABEL_NONE, (void *)Stopwatch_OnRun },
	{ "MoButton", "Stop",      "0",        0,  21,  82, 40, 20, LABEL_NONE, (void *)Stopwatch_OnStop },
	{ "Dropdown", "RunEdge",   "Positive", 0,  96,  54, 63, 14, LABEL_NONE },
	{ "Dropdown", "StopEdge",  "Run Ends", 0,  96,  94, 63, 14, LABEL_NONE },
	{ "LED",      "OnEvent",   "0",        0, 214,  52, 10, 10, LABEL_NONE },
	{ "LED",      "OffEvent",  "1",        0, 214,  88, 10, 10, LABEL_NONE },
	{ "Dropdown", "TimeUnits", "msecs",    0,  96, 142, 63, 14, LABEL_NONE, (void *)Stopwatch_OnTimeUnits },
	{ "TextOut",  "Duration",  "",         0, 185, 143, 66, 13, LABEL_NONE },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);
	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Stopwatch");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, StopwatchPanel);

	/* the dropdowns' backing lists and the lifecycle state - no on-screen control */
	PublishProp(ClassSelf, "RunEdgeList",   "data", PROP_NULL, "Positive,Negative");
	PublishProp(ClassSelf, "StopEdgeList",  "data", PROP_NULL, "Run Ends,Positive,Negative");
	PublishProp(ClassSelf, "TimeUnitsList", "data", PROP_NULL, "msecs,secs");
	PublishProp(ClassSelf, "State",         "data", PROP_LED, "1");

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

	SetName(temp, "Stopwatch");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "c48b1e37-05a9-4d72-8f61-7b3e2a9c604d");
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
