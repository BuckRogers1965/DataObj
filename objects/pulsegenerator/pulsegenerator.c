
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

PulseGenerator object: a self-contained pulse-generator instrument panel. It
owns its own timing task and toggles its Out/~Out LEDs by Period and Duty
Cycle - the panel and the clock are one object.

This is a NEW widget; the existing task-driven `Pulse` object (objects/pulse,
Interval/Count model) is left untouched. Where TCPPort is a shell driving a
separate engine, the pulse generator's engine is its own timing loop, so it
carries it directly - a widget built on the same panel mechanics (deferred
build, sub-panel, reflect-and-seed) with the clock inside.

Dataflow: the default input connection is to the Start button, the default
output connection is from the Out LED - so Start is the natural input and Out
carries the "1"/"0" pulse downstream.

*/

/* NO hardcoded help text: the help lives in objects/pulsegenerator/README.md
   and is read from disk into the Help box (PulseGen_Ctl). */

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates everything */
	int     running;	/* is a cycle in progress? (the Active state)          */
	int     high;		/* is Out currently high (1) or low (0)?               */
	TaskObj task;		/* the clock; one per instance life, re-armed per phase */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem PulseGenPanel[];
static int  PulseGen_Tick(NodeObj instance, NodeObj data, int reason);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("PulseGenerator handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the clock ------------------------------------------------------ */

/* Period and Duty Cycle are read LIVE, not snapshotted at Start, so a    */
/* running generator can be retuned from the panel - each phase computes  */
/* its width fresh, the same discipline Pulse uses for its Interval.      */
static void PulseGen_Widths(NodeObj instance, int *onW, int *offW)
{
	int period = GetPropInt(instance, "Period");
	int duty   = GetPropInt(instance, "DutyCycle");
	int on;

	if (period < 1)
		period = 1000;
	if (duty < 0)
		duty = 0;
	if (duty > 100)
		duty = 100;

	on = period * duty / 100;
	if (on < 1)
		on = 1;			/* never a zero-length phase */
	*onW = on;
	*offW = (period - on) < 1 ? 1 : (period - on);
}

/* Out and ~Out are just properties. Setting Out fans out to everything
   subscribed to it - the Out LED, and anything a flow has wired to it -
   the write IS the event; there is no special "out port". ~Out is its
   inverse, another plain property. */
static void PulseGen_Emit(NodeObj instance, int high)
{
	SetPropStr(instance, "Out", high ? "1" : "0");
	SetPropStr(instance, "NotOut", high ? "0" : "1");
}

/* arm the next phase */
static void PulseGen_Arm(NodeObj instance, InstanceData *local, int millis)
{
	AddTaskMilli(local->task, millis, (FuncPtr)PulseGen_Tick, msg_send, instance);
}

/* at the end of a phase, toggle. High for                                 */
/* onWidth, low for offWidth, repeat - unless Single Shot, which ends the  */
/* run after the first high pulse falls (one cycle then halt).             */
static int PulseGen_Tick(NodeObj instance, NodeObj data, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int onW, offW;

	(void) data;

	if (reason == task_deactivate)
		return rtrn_handled;
	if (!local || !local->running)
		return rtrn_dropped;
	if (!local->enabled)
		return rtrn_handled;		/* a disabled panel has already been stopped */

	PulseGen_Widths(instance, &onW, &offW);

	if (local->high)
	{
		/* the high phase is over: go low */
		PulseGen_Emit(instance, 0);
		local->high = 0;

		if (GetPropInt(instance, "OneShot"))
		{
			/* one cycle only: the pulse has fallen, halt here */
			SetPropStr(instance, "Active", "0");
			local->running = 0;
			return rtrn_handled;
		}

		PulseGen_Arm(instance, local, offW);
	}
	else
	{
		/* the low phase is over: go high again (continuous) */
		PulseGen_Emit(instance, 1);
		local->high = 1;
		PulseGen_Arm(instance, local, onW);
	}

	return rtrn_handled;
}

/* Start: begin a cycle. If already running, restart only when            */
/* Retriggerable; otherwise the press is ignored.                         */
static void PulseGen_DoStart(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int onW, offW;

	if (!local || !local->enabled)
		return;
	if (local->running && !GetPropInt(instance, "Retriggerable"))
		return;

	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());

	PulseGen_Widths(instance, &onW, &offW);

	local->running = 1;
	SetPropStr(instance, "Active", "1");

	/* the cycle opens on the rising edge */
	PulseGen_Emit(instance, 1);
	local->high = 1;
	PulseGen_Arm(instance, local, onW);
}

/* Stop: halt and leave the line low */
static void PulseGen_DoStop(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return;

	if (local->high)
		PulseGen_Emit(instance, 0);	/* fall to 0 if we were high */
	local->high = 0;
	local->running = 0;
	SetPropStr(instance, "Active", "0");
	/* no re-arm: a still-pending tick sees !running and returns */
}

/* ---- action handlers ----------------------------------------------- */

/* Start is an ordinary property carrying a handler; a write of 1 acts -
   a MoButton, a Pulse, or a script all write it the same way */
int PulseGen_OnStart(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	PulseGen_DoStart(instance);
	return rtrn_handled;
}

int PulseGen_OnStop(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	PulseGen_DoStop(instance);
	return rtrn_handled;
}

/* Enable: 1 allows operation, 0 is a full stop - disabling presses Stop,
   enabling honors Run-when-Enabled */
int PulseGen_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	if (!local->enabled)
		PulseGen_DoStop(instance);
	else if (GetPropInt(instance, "AutoStart"))
		PulseGen_DoStart(instance);

	return rtrn_handled;
}

/* Placement setup, run when the widget is placed: settle the LEDs low
   and honor Run-when-Enabled. Registered as
   the framework's Activate hook, and run once by the build task so the
   panel comes up live (gated only by Enable, which defaults on).         */
int PulseGen_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, PulseGenPanel);

	/* resting state: line low, ~Out lit, not active */
	local->high = 0;
	local->running = 0;
	SetPropStr(instance, "Active", "0");
	SetPropStr(instance, "NotOut", "1");

	if (local->enabled && GetPropInt(instance, "AutoStart"))
		PulseGen_DoStart(instance);

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->running = 0;
	local->high = 0;
	local->task = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "PulseGenerator");

	/* every control's value + handler from the table (Start/Stop/Enable carry
	   a handler; Period/DutyCycle/the LEDs/option checkboxes are plain data) */
	Widget_Init(instance, PulseGenPanel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)PulseGen_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, PulseGenPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, PulseGenPanel);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, and every control (value +,
   for the ports, handler). State is published apart (lifecycle, no control). */
static WidgetItem PulseGenPanel[] = {
	/* cls        prop            def    panel   x    y    w    h  label       [handler] */
	{ "View",     "PulseGenerator", "",  0,   0,   0, 300, 250, 0 },			/* 0: main */
	{ "Help",     "objects/pulsegenerator/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",        "1",   0, 206,  13,   9,  9, LABEL_LEFT, (void *)PulseGen_OnEnable },
	{ "MoButton", "Start",         "0",   0,  19,  46,  40, 20, LABEL_NONE, (void *)PulseGen_OnStart },
	{ "MoButton", "Stop",          "0",   0,  19,  75,  40, 20, LABEL_NONE, (void *)PulseGen_OnStop },
	{ "Textbox",  "Period",        "1000",0,  99,  46, 120, 20, LABEL_NONE },
	{ "Textbox",  "DutyCycle",     "50",  0,  99,  86, 120, 20, LABEL_NONE },
	{ "LED",      "Out",           "0",   0, 233,  45,  12, 12, LABEL_NONE },
	{ "LED",      "NotOut",        "1",   0, 233,  80,  12, 12, LABEL_NONE },
	{ "LED",      "Active",        "0",   0,  32, 132,  12, 12, LABEL_NONE },
	{ "Checkbox", "OneShot",       "1",   0,  99, 128,   9,  9, LABEL_NONE },
	{ "Checkbox", "Retriggerable", "0",   0,  99, 146,   9,  9, LABEL_NONE },
	{ "Checkbox", "AutoStart",     "0",   0,  99, 164,   9,  9, LABEL_NONE },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */
	if (local)
	{
		if (local->task)				/* stop the clock before freeing local */
			DeleteTask(local->task);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "PulseGenerator");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, PulseGenPanel);
	PublishProp(ClassSelf, "State", "data", PROP_LED, "1");	/* lifecycle, no control */

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

	SetName(temp, "PulseGenerator");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "a3f9d21c-7e64-4b8a-9c15-2d0e83b6f512");
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
