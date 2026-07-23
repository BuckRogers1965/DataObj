
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

LogicGate object: a logic-gate instrument panel. Built on the same panel
mechanics as PulseGenerator (deferred build, sub-panel, reflect-and-seed,
help-on-open). It combines its In by the chosen Mode (OR / AND / XOR /
Parity) with an optional Invert and publishes the result on Out.

As a single-input OR with Invert on, it is a NOT gate - which turns a Pulse
Generator's Out into its inverse: feed pulse Out into In, wire Out into a
second Stopwatch's Run, and that stopwatch times the LOW phase while the
first (on the pulse directly) times the HIGH phase - both duty cycles.

Everything is a property. In/Interpret/Enable carry a handler so a write
acts; Mode/Invert/ChangesOnly/AutoInterpret are plain data read live; Out
is a plain property whose write fans out to whatever is wired to it. In and
Out are ordinary properties named In and Out - not a special "port" kind.

NOTE on multiple inputs: this framework delivers one value at a time and does
not yet expose a port's sources, so OR/AND/XOR here operate on the single
arriving value (identity for one input) and Parity toggles per event -
faithful for the inverter/buffer use. True N-input combination waits on the
source-enumeration primitive (ROADMAP.md, Phase 8).

*/

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates the gate       */
	int     lastInput;	/* the most recent value seen on In           */
	int     parity;		/* running parity state for Parity mode       */
	int     panelBuilt;	/* the panel is built once, when it has a path */
	TaskObj buildTask;	/* fires one tick after creation, to build     */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void LogicGate_BuildPanel(NodeObj instance);
static int  LogicGate_BuildTask(NodeObj instance, NodeObj data, int msgid);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("LogicGate handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the logic ------------------------------------------------------ */

static int LogicGate_Is(NodeObj instance, char *prop, char *val)
{
	char *cur = GetPropStr(instance, prop);
	return cur && strcmp(cur, val) == 0;
}

/* single-input case: OR/AND/XOR of one
   value is that value; Parity toggles on each event; Invert flips. */
static int LogicGate_Compute(NodeObj instance, int in)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int r;

	if (LogicGate_Is(instance, "GateMode", "Parity Gate"))
	{
		local->parity = !local->parity;
		r = local->parity;
	}
	else
	{
		r = in ? 1 : 0;
	}

	if (GetPropInt(instance, "InvertOp"))
		r = !r;

	return r;
}

/* Out is just a property. Setting it fans out to everything subscribed -
   the Out LED, and anything a flow wired to it - honoring Changes Only. */
static void LogicGate_Emit(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int r = LogicGate_Compute(instance, local->lastInput);

	if (GetPropInt(instance, "ChangesOnly") && GetPropInt(instance, "Out") == r)
		return;

	SetPropStr(instance, "Out", r ? "1" : "0");
}

/* ---- action handlers ------------------------------------------------ */

/* In: a value arrived from a wired source. Remember it, and (enabled and
   Auto Interpret on) recompute and publish. */
int LogicGate_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	local->lastInput = GetValueInt(data) ? 1 : 0;

	if (!local->enabled)
		return rtrn_handled;
	if (!GetPropInt(instance, "AutoInterpret"))
		return rtrn_handled;		/* wait for Interpret */

	LogicGate_Emit(instance);
	return rtrn_handled;
}

/* Interpret: recompute now from the last input (for use when Auto Interpret
   is off) */
int LogicGate_OnInterpret(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local || !local->enabled)
		return rtrn_handled;

	LogicGate_Emit(instance);
	return rtrn_handled;
}

/* Enable: gates the gate */
int LogicGate_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* Placement setup - run once by the build task so the panel comes up live
   (gated only by Enable, which defaults on). */
int LogicGate_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		LogicGate_BuildPanel(instance);
	}

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

static void LogicGate_Port(NodeObj instance, char *name, char *initial, void *handler)
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
	local->lastInput = 0;
	local->parity = 0;
	local->panelBuilt = 0;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "LogicGate");

	/* the mode selector and its option list */
	SetPropStr(instance, "GateMode", "OR Gate");
	SetPropStr(instance, "GateModeList", "OR Gate,AND Gate,XOR Gate,Parity Gate");

	/* the option checkboxes - plain data, read live by the logic */
	SetPropStr(instance, "InvertOp", "0");
	SetPropStr(instance, "ChangesOnly", "1");
	SetPropStr(instance, "AutoInterpret", "1");

	/* Out - the result; just a property, the LED reflects it and downstream
	   wires from it */
	SetPropStr(instance, "Out", "0");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)LogicGate_Activate);

	/* the things that ACT when written - In (input), Interpret, Enable.
	   In and Out are ordinary properties named In and Out. */
	LogicGate_Port(instance, "In",        "0", (void *)LogicGate_OnIn);
	LogicGate_Port(instance, "Interpret", "0", (void *)LogicGate_OnInterpret);
	LogicGate_Port(instance, "Enable",    "1", (void *)LogicGate_OnEnable);

	InitPosition(instance);

	/* the view's OWN size, set before any client can subscribe */
	SetPropInt(instance, "W", 300);
	SetPropInt(instance, "H", 250);

	RegisterInstance(class, instance);

	/* arm the deferred build: populated one tick from now, after the bridge
	   has given this instance its path */
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)LogicGate_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control) AND seed it now, so the GUI
   shows the underlying value the moment the control is created (see TCPPort) */
static void LogicGate_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees) */
static char *LogicGate_ReadFile(char *path)
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

/* one control put into a container view and wired to the gate's own
   property - a widget dropped into a view. The kind of wire follows the
   control class, exactly as in PulseGenerator/TCPPort. */
static void LogicGate_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
						  int x, int y, int w, int h, int rows, int cols)
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

	if (strcmp(cls, "Textbox") == 0 && rows > 0 && cols > 0)
	{
		SetPropInt(c, "Rows", rows);
		SetPropInt(c, "Cols", cols);
	}

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);		/* a command property */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
	{
		/* the Help box starts EMPTY; its README.md is read from disk and set
		   into its Value only when the Help panel is OPENED
		   (LogicGate_OnHelpOpen resolves it by path) - never eagerly. */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		LogicGate_Reflect(target, prop, c, "Value");	/* set its display property */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);			/* the pick drives the value */
		LogicGate_Reflect(target, listprop, c, "Items");	/* its options */
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));	/* show the pick */
	}
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);		/* edits it */
		LogicGate_Reflect(target, prop, c, "In");	/* and reflects it, seeded now */
	}
}

/* a sub-panel: a View put into the panel (renders as an icon that opens),
   then populated with its own controls - a view inside a view. */
static NodeObj LogicGate_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
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

/* the Help panel was OPENED: its Open was delivered here (not stored). Read
   the widget's README.md from disk and set it into the Help box's Value with
   an update. No hardcoded help - the file is the source, loaded on open. */
int LogicGate_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN (Open -> 1) */

	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = LogicGate_ReadFile("objects/logicgate/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

/* The panel: 0 = main, 1 = Help. The Out LED
   shows the result. */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } LGCtl;

static LGCtl LogicGatePanel[] = {
	/* --- panel 0: Logic Gate (the object's own view) --- */
	{ "Checkbox", "Enable",        148,  13,   8,  8, 0,  0,  0 },
	{ "Dropdown", "GateMode",           15,  57, 178, 15, 0,  0,  0 },
	{ "Checkbox", "InvertOp",        16,  89,   8,  8, 0,  0,  0 },
	{ "Checkbox", "ChangesOnly",    108,  89,   8,  8, 0,  0,  0 },
	{ "Checkbox", "AutoInterpret",  108, 119,   8,  8, 0,  0,  0 },
	{ "MoButton", "Interpret",      108, 141,  60, 20, 0,  0,  0 },
	{ "LED",      "Out",             14, 118,  12, 12, 0,  0,  0 },

	/* --- panel 1: Help --- */
	/* the standard help box: fills the Help panel with a 10px margin */
	{ "Markdown", "HelpText",        10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

/* build the panel: panel 0 goes straight into the object, the Help panel is
   a sub-view rendering as an openable icon. */
static void LogicGate_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = LogicGate_SubPanel(instance, "Help", 10, 188, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED */
	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)LogicGate_OnHelpOpen);
	}

	for (i = 0; LogicGatePanel[i].cls; i++)
	{
		LGCtl *t = &LogicGatePanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			LogicGate_Ctl(container, instance, t->cls, t->prop,
						  t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

static int LogicGate_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		LogicGate_BuildPanel(instance);
		LogicGate_Activate(instance, msg_initialize, NULL);
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

	SetName(class, "LogicGate");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* EVERYTHING is a property. In/Interpret/Enable carry handlers so a write
	   acts; the rest is plain data read live. In and Out are ordinary
	   properties named In and Out - not a special port kind. */
	PublishProp(ClassSelf, "Enable",        "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "GateMode",          "data", PROP_MENU, "OR Gate");
	PublishProp(ClassSelf, "GateModeList",      "data", PROP_NULL, "OR Gate,AND Gate,XOR Gate,Parity Gate");
	PublishProp(ClassSelf, "InvertOp",      "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "ChangesOnly",   "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "AutoInterpret", "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Interpret",     "data", PROP_NULL, "0");

	PublishProp(ClassSelf, "In",            "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "Out",           "data", PROP_LED, "0");

	PublishProp(ClassSelf, "State",         "data", PROP_LED, "1");
	/* no HelpText property: the Help box loads README.md from disk */

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

	SetName(temp, "LogicGate");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b6027f4a-3c81-49e5-a2d0-71f4c8e5309b");
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
