
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "timer.h"
#include "DebugPrint.h"

/*

Stopwatch object: the stop-watch instrument panel, ported from the VNOS
control of the same name (objects/demo/stopwatch - stopwatch.c and its
panel definition stopwatchpb.c). It measures and reports the time between
a start event and a stop event.

Wire a PulseGenerator's Out to this Run and it TIMES THE PULSES: with Run
Edge = Positive and Stop Edge = Run Ends, timing starts on the rising
edge and stops on the falling edge, so Duration reads the pulse's high
width. (Point Stop Edge at a separate Stop line to time between two
different events instead.)

	VNOS                          here
	----                          ----
	stopwatchpb.c ControlInfo[]   the flat control table (SWCtl)
	Run_change / Stop_change      Stopwatch_OnRun / Stopwatch_OnStop
	vGet64bMilliSecTime           GetCurrentTime (timer.h, cached us clock)
	Run/Stop Edge menus           Dropdowns over RunEdge/StopEdge
	Duration static text          the Duration property (a readout)

Everything here is a property. Run/Stop/Enable carry a handler so a write
acts; there is no "in"/"out" port. A view exposes its external
inputs/outputs as aliases onto these properties.

Dataflow, as the reference help states it: "Default input connection is
to Run. Default output connection is from Duration."

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
	int           panelBuilt;
	TaskObj       buildTask;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void Stopwatch_BuildPanel(NodeObj instance);
static int  Stopwatch_BuildTask(NodeObj instance, NodeObj data, int msgid);

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

/* render the last measurement into Duration, in the chosen Time Scale -
   the reference's stpwShowDuration. Duration is just a property, so the
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

/* the reference's stpwStopTimer: close the interval and publish it */
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
   stops it - the reference's Run_change. */
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

/* Stop: Positive stops on 1, Negative on 0 - the reference's Stop_change */
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

/* Time Scale changed: re-display the same measurement in the new unit -
   the reference's TimeUnits_change. */
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

/* Placement setup - the reference's ACTIVATE handler: settle the LEDs
   (Off lit, On dark) and clear any in-progress state. */
int Stopwatch_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Stopwatch_BuildPanel(instance);
	}

	local->active = 0;
	SetPropStr(instance, "OnEvent", "0");
	SetPropStr(instance, "OffEvent", "1");

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

static void Stopwatch_Handler(NodeObj instance, char *name, char *initial, void *handler)
{
	NodeObj port;

	SetPropStr(instance, name, initial);
	port = GetPropNode(instance, name);
	SetPropLong(port, "OnMsg", (long)handler);
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->enabled = 1;
	local->active = 0;
	local->startSec = 0;
	local->startUsec = 0;
	local->lastMs = -1;
	local->panelBuilt = 0;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Stopwatch");

	/* the edge / scale selectors and their option lists (a Dropdown reads
	   its options from the companion *List property) */
	SetPropStr(instance, "RunEdge", "Positive");
	SetPropStr(instance, "RunEdgeList", "Positive,Negative");
	SetPropStr(instance, "StopEdge", "Run Ends");
	SetPropStr(instance, "StopEdgeList", "Run Ends,Positive,Negative");
	SetPropStr(instance, "TimeUnits", "msecs");
	SetPropStr(instance, "TimeUnitsList", "msecs,secs");

	/* the readout and the two state LEDs */
	SetPropStr(instance, "Duration", "");
	SetPropStr(instance, "OnEvent", "0");
	SetPropStr(instance, "OffEvent", "1");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Stopwatch_Activate);

	/* the controls that ACT when written - ordinary properties, each with
	   a handler; TimeUnits re-displays on change */
	Stopwatch_Handler(instance, "Run",    "0", (void *)Stopwatch_OnRun);
	Stopwatch_Handler(instance, "Stop",   "0", (void *)Stopwatch_OnStop);
	Stopwatch_Handler(instance, "Enable", "1", (void *)Stopwatch_OnEnable);
	Stopwatch_Handler(instance, "TimeUnits", "msecs", (void *)Stopwatch_OnTimeUnits);

	InitPosition(instance);

	/* the view's OWN size, set before any client can subscribe - the
	   reference main panel (265x216), grown for the Dropdowns and readout */
	SetPropInt(instance, "W", 300);
	SetPropInt(instance, "H", 250);

	RegisterInstance(class, instance);

	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)Stopwatch_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control's input) AND seed it now, so the
   GUI shows the underlying value at creation (see TCPPort / PulseGenerator) */
static void Stopwatch_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

static void Stopwatch_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
						  int x, int y, int w, int h)
{
	char cpath[256], path[300];
	NodeObj c = CreateObject(container, cls);
	if (!c)
		return;

	if (PathOfInstance(container, cpath, sizeof(cpath)))
	{
		SetPropStr(c, "Name", prop && prop[0] ? prop : cls);
		snprintf(path, sizeof(path), "%s/%s", cpath, prop && prop[0] ? prop : cls);
		RegisterPath(path, c);
	}

	SetPropInt(c, "X", x);
	SetPropInt(c, "Y", y);
	SetPropInt(c, "W", w);
	SetPropInt(c, "H", h);
	if (prop && prop[0])
		SetPropStr(c, "Label", prop);

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);		/* a command property */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
	{
		/* the Help box starts EMPTY; its README.md is read from disk into its
		   Value only when the Help panel is OPENED (Stopwatch_OnHelpOpen). */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		Stopwatch_Reflect(target, prop, c, "Value");	/* set its display property */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);			/* the pick drives the value */
		Stopwatch_Reflect(target, listprop, c, "Items");	/* its options */
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));	/* show the pick */
	}
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);
		Stopwatch_Reflect(target, prop, c, "In");
	}
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees) */
static char *Stopwatch_ReadFile(char *path)
{
	FILE *f = fopen(path, "rb");
	long  n;
	char *buf;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0)
	{
		fclose(f);
		return NULL;
	}
	buf = malloc(n + 1);
	if (!buf)
	{
		fclose(f);
		return NULL;
	}
	n = (long)fread(buf, 1, n, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

/* the Help panel was OPENED: read the widget's README.md from disk and set
   it into the Help box's Value with an update. No hardcoded help - the file
   is the source, loaded on open. */
int Stopwatch_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN */

	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = Stopwatch_ReadFile("objects/stopwatch/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

static NodeObj Stopwatch_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
{
	char ppath[256], path[300];
	NodeObj v = CreateObject(panel, "View");
	if (!v)
		return NULL;
	SetPropStr(v, "Name", name);
	if (PathOfInstance(panel, ppath, sizeof(ppath)))
	{
		snprintf(path, sizeof(path), "%s/%s", ppath, name);
		RegisterPath(path, v);
	}
	SetPropInt(v, "X", x);
	SetPropInt(v, "Y", y);
	SetPropInt(v, "W", w);
	SetPropInt(v, "H", h);
	return v;
}

/* the panel, straight off the reference's ControlInfo[] table
   (objects/demo/stopwatch/stopwatchpb.c): 0 = main panel, 1 = Help */
typedef struct { char *cls, *prop; int x, y, w, h, panel; } SWCtl;

static SWCtl StopwatchPanel[] = {
	/* --- panel 0: Stop Watch (the object's own view) --- */
	{ "Checkbox", "Enable",     205,  13,   9,  9, 0 },
	{ "MoButton", "Run",         21,  52,  40, 20, 0 },
	{ "MoButton", "Stop",        21,  82,  40, 20, 0 },
	{ "Dropdown", "RunEdge",     96,  54,  63, 14, 0 },
	{ "Dropdown", "StopEdge",    96,  94,  63, 14, 0 },
	{ "LED",      "OnEvent",     214, 52,  10, 10, 0 },
	{ "LED",      "OffEvent",    214, 88,  10, 10, 0 },
	{ "Dropdown", "TimeUnits",   96, 142,  63, 14, 0 },
	{ "TextOut",  "Duration",   185, 143,  66, 13, 0 },

	/* --- panel 1: Help --- */
	/* the standard help box: fills the Help panel with a 10px margin */
	{ "Markdown", "HelpText",    10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1 },

	{ NULL, NULL, 0, 0, 0, 0, 0 }
};

static void Stopwatch_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = Stopwatch_SubPanel(instance, "Help", 12, 188, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED */
	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)Stopwatch_OnHelpOpen);
	}

	for (i = 0; StopwatchPanel[i].cls; i++)
	{
		SWCtl *t = &StopwatchPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			Stopwatch_Ctl(container, instance, t->cls, t->prop, t->x, t->y, t->w, t->h);
	}
}

static int Stopwatch_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		Stopwatch_BuildPanel(instance);
		Stopwatch_Activate(instance, msg_initialize, NULL);
	}

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->buildTask)
			RemoveTask(local->buildTask);
		free(local);
	}

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

	/* EVERYTHING is a property - Run/Stop/Enable/TimeUnits carry handlers so
	   a write acts, but they are ordinary data like the rest. No in/out port. */
	PublishProp(ClassSelf, "Enable",        "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Run",           "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "Stop",          "data", PROP_NULL, "0");

	PublishProp(ClassSelf, "RunEdge",       "data", PROP_MENU, "Positive");
	PublishProp(ClassSelf, "RunEdgeList",   "data", PROP_NULL, "Positive,Negative");
	PublishProp(ClassSelf, "StopEdge",      "data", PROP_MENU, "Run Ends");
	PublishProp(ClassSelf, "StopEdgeList",  "data", PROP_NULL, "Run Ends,Positive,Negative");
	PublishProp(ClassSelf, "TimeUnits",     "data", PROP_MENU, "msecs");
	PublishProp(ClassSelf, "TimeUnitsList", "data", PROP_NULL, "msecs,secs");

	PublishProp(ClassSelf, "OnEvent",       "data", PROP_LED, "0");
	PublishProp(ClassSelf, "OffEvent",      "data", PROP_LED, "1");
	PublishProp(ClassSelf, "Duration",      "data", PROP_TEXTOUT, "");

	PublishProp(ClassSelf, "State",         "data", PROP_LED, "1");
	/* no HelpText property: the Help box loads README.md from disk on open */

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
