
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

PulseGenerator object: the pulse-generator instrument panel, ported from
the VNOS control of the same name (objects/demo/pulsegenerator - pulse.c
and its panel definition pulsepb.c). It is a SELF-CONTAINED widget: it
owns its own timing task and toggles its Out/~Out LEDs by Period and
Duty Cycle, exactly as the reference's plsgRunClock did - the panel and
the clock are one object, as they were in VNOS.

This is a NEW widget; the existing task-driven `Pulse` object
(objects/pulse, Interval/Count model) is left untouched. Where TCPPort is
a shell driving a separate engine, the pulse generator's engine is its
own timing loop, so it carries it directly - a widget built on the same
panel mechanics (deferred build, sub-panel, reflect-and-seed) with the
reference's clock inside.

	VNOS                          here
	----                          ----
	pulsepb.c ControlInfo[]       the flat control table (PGCtl), one row
	                              per control at the reference's x/y/w/h
	plsgRunClock state machine    PulseGen_Tick (the toggling task)
	Cmd Start/Stop                ordinary properties carrying a handler
	                              (a MoButton, a Pulse, or a script writes
	                              them the same way to act)
	Out / ~Out / Active LEDs      PROP_LED properties
	Period / DutyCycle            plain data, read LIVE each phase so a
	                              running generator can be retuned

Dataflow, as the reference help states it: "Default input connection is
to the Start button. Default output connection is from the Out LED." So
Start is the natural input and Out carries the "1"/"0" pulse downstream.

*/

/* NO hardcoded help text: the help lives in objects/pulsegenerator/README.md
   and is read from disk into the Help box (PulseGen_Ctl). */

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates everything, like the reference */
	int     running;	/* is a cycle in progress? (the Active state)          */
	int     high;		/* is Out currently high (1) or low (0)?               */
	int     panelBuilt;	/* the panel is built once, when the object has a path  */
	TaskObj task;		/* the clock; one per instance life, re-armed per phase */
	TaskObj buildTask;	/* fires one tick after creation, to build the panel   */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void PulseGen_BuildPanel(NodeObj instance);
static int  PulseGen_BuildTask(NodeObj instance, NodeObj data, int msgid);
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
		on = 1;			/* never a zero-length phase - the reference's bug 1644 */
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

/* the reference's plsgRunClock: at the end of a phase, toggle. High for   */
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
/* Retriggerable; otherwise the press is ignored (the reference's         */
/* Start_change).                                                         */
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

/* Stop: halt and leave the line low - the reference's Stop_change */
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

/* Enable: 1 allows operation, 0 is a full stop (the reference's
   Enable_change - disabling presses Stop, enabling honors Run-when-Enabled) */
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

/* Placement setup - the reference's inctActivatedTask when the widget is
   placed: settle the LEDs low and honor Run-when-Enabled. Registered as
   the framework's Activate hook, and run once by the build task so the
   panel comes up live (gated only by Enable, which defaults on).         */
int PulseGen_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		PulseGen_BuildPanel(instance);
	}

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

static void PulseGen_Port(NodeObj instance, char *name, char *initial, void *handler)
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
	local->running = 0;
	local->high = 0;
	local->panelBuilt = 0;
	local->task = NULL;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "PulseGenerator");

	/* timing */
	SetPropStr(instance, "Period", "1000");
	SetPropStr(instance, "DutyCycle", "50");

	/* the LEDs - just properties; subscribers (LEDs, downstream wires)
	   attach to Out and see every write to it */
	SetPropStr(instance, "Out", "0");
	SetPropStr(instance, "NotOut", "1");
	SetPropStr(instance, "Active", "0");

	/* the options - the reference's defaults */
	SetPropStr(instance, "OneShot", "1");
	SetPropStr(instance, "Retriggerable", "0");
	SetPropStr(instance, "AutoStart", "0");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)PulseGen_Activate);

	/* the commands and the enable line - ordinary properties, each
	   carrying a handler that acts when the property is written */
	PulseGen_Port(instance, "Start",  "0", (void *)PulseGen_OnStart);
	PulseGen_Port(instance, "Stop",   "0", (void *)PulseGen_OnStop);
	PulseGen_Port(instance, "Enable", "1", (void *)PulseGen_OnEnable);

	InitPosition(instance);

	/* the view's OWN size, set here as a resting value BEFORE any client can
	   subscribe - the reference main panel (265x216), grown for the
	   character-sized TimeBase / Duty Cycle boxes and the Help icon. */
	SetPropInt(instance, "W", 300);
	SetPropInt(instance, "H", 250);

	RegisterInstance(class, instance);

	/* arm the deferred build: the panel is populated one tick from now,
	   after the bridge has given this instance its path. */
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)PulseGen_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control's input) AND seed it: hand the
   control the property's CURRENT value now, so the GUI shows the
   underlying value the moment the control is created (see TCPPort). */
static void PulseGen_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees).
   The framework runs from the project root, so the widget's own docs are
   at objects/<name>/README.md. */
static char *PulseGen_ReadFile(char *path)
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

/* one control put into a container view and wired to the generator's own
   property - a widget dropped into a view. The kind of wire follows the
   control class, exactly as in TCPPort. */
static void PulseGen_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
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
		/* the Help box starts EMPTY. Its README.md is read from disk and set
		   into its Value only when the Help panel is OPENED
		   (PulseGen_OnHelpOpen resolves it by path) - never eagerly. */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		PulseGen_Reflect(target, prop, c, "Value");	/* set its display property */
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);		/* edits it */
		PulseGen_Reflect(target, prop, c, "In");	/* and reflects it, seeded now */
	}
}

/* a sub-panel: a View put into the panel (renders as an icon that opens),
   then populated with its own controls - a view inside a view. */
static NodeObj PulseGen_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
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

/* the Help panel was OPENED: its Open was delivered here (not stored - the
   open state is not saved in the property). Read the widget's README.md from
   disk and set it into the Help box's Value with an update. There is no
   hardcoded help - the file is the source, loaded on open. */
int PulseGen_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN (Open -> 1) */

	/* resolve the box BY PATH, the same way the client subscribes to it, so
	   the write lands on the node the client is actually watching */
	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = PulseGen_ReadFile("objects/pulsegenerator/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

/* The panel, straight off the reference's ControlInfo[] table
   (objects/demo/pulsegenerator/pulsepb.c): one flat table, every control
   tagged with the panel it lives on - 0 = the main panel (the object
   itself), 1 = Help. Same x, y, w, h. */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } PGCtl;

static PGCtl PulseGenPanel[] = {
	/* --- panel 0: Pulse Generator (the object's own view) --- */
	{ "Checkbox", "Enable",        206,  13,   9,  9, 0,  0,  0 },
	{ "MoButton", "Start",          19,  46,  40, 20, 0,  0,  0 },
	{ "MoButton", "Stop",           19,  75,  40, 20, 0,  0,  0 },
	{ "Textbox",  "Period",         99,  46, 112, 15, 0,  1, 14 },
	{ "Textbox",  "DutyCycle",      99,  86, 112, 15, 0,  1, 14 },
	{ "LED",      "Out",           233,  45,  12, 12, 0,  0,  0 },
	{ "LED",      "NotOut",        233,  80,  12, 12, 0,  0,  0 },
	{ "LED",      "Active",         32, 132,  12, 12, 0,  0,  0 },
	{ "Checkbox", "OneShot",        99, 128,   9,  9, 0,  0,  0 },
	{ "Checkbox", "Retriggerable",  99, 146,   9,  9, 0,  0,  0 },
	{ "Checkbox", "AutoStart",      99, 164,   9,  9, 0,  0,  0 },

	/* --- panel 1: Help --- */
	/* the standard help box: fills the Help panel with a 10px margin */
	{ "Markdown", "HelpText",       10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

/* build the panel: panel 0 goes straight into the object (its own view),
   the Help panel is a sub-view rendering as an openable icon where the
   reference's Help button sat. */
static void PulseGen_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = PulseGen_SubPanel(instance, "Help", 10, 188, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED: the
	   sub-view's Open is delivered to this handler (it does NOT store the
	   open state - it just triggers the load). */
	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)PulseGen_OnHelpOpen);
	}

	for (i = 0; PulseGenPanel[i].cls; i++)
	{
		PGCtl *t = &PulseGenPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			PulseGen_Ctl(container, instance, t->cls, t->prop,
						 t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

static int PulseGen_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		PulseGen_BuildPanel(instance);
		/* come up live and settled (and AutoStart, if set) */
		PulseGen_Activate(instance, msg_initialize, NULL);
	}

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		/* stop the clock before freeing local, or a still-scheduled tick
		   fires later with a dangling instance pointer as its data */
		if (local->buildTask)
			RemoveTask(local->buildTask);
		if (local->task)
			DeleteTask(local->task);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	NodeObj entry;

	SetName(class, "PulseGenerator");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* EVERYTHING is a property - there is no "in"/"out" port. Start/Stop/
	   Enable carry a handler (OnMsg) so a write to them acts, but they are
	   ordinary data properties like the rest; Out is a data property whose
	   write fans out to whatever is wired to it. A view exposes its external
	   inputs/outputs as ALIASES onto these, not as a special port kind. */
	PublishProp(ClassSelf, "Enable",        "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Start",         "data", PROP_NULL, "");
	PublishProp(ClassSelf, "Stop",          "data", PROP_NULL, "");

	entry = PublishProp(ClassSelf, "Period", "data", PROP_TEXTBOX, "1000");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 14);
	entry = PublishProp(ClassSelf, "DutyCycle", "data", PROP_TEXTBOX, "50");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 14);

	/* the pulse output and its inverse, and the activity light */
	PublishProp(ClassSelf, "Out",           "data", PROP_LED, "0");
	PublishProp(ClassSelf, "NotOut",        "data", PROP_LED, "1");
	PublishProp(ClassSelf, "Active",        "data", PROP_LED, "0");

	PublishProp(ClassSelf, "OneShot",       "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Retriggerable", "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "AutoStart",     "data", PROP_CHECKBOX, "0");

	PublishProp(ClassSelf, "State",         "data", PROP_LED, "1");
	/* no HelpText property: the Help box loads README.md from disk */

	/* the panel is not declared here - this widget BUILDS it
	   (PulseGen_BuildPanel): a View with controls in it. */

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
