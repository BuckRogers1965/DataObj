
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Skeleton widget: a copyable template for an instrument-panel widget. Copy
it with objects/skeleton/newwidget.sh (see README.md) to start a new one.

A widget is a COMPOSITE VIEW: its controls are ordinary control instances
(Checkbox, MoButton, LED, Textbox, Dropdown, TextOut, Markdown, ...) laid
out inside it by their X/Y. The object does not draw anything - the view
renders whatever the object declares. Everything is a property; controls
are wired to the widget's own properties with Connect().

This skeleton demonstrates every pattern that today's widgets settled on -
see README.md for the WHY behind each. NO hardcoded help: the Help box
loads objects/skeleton/README.md from disk when its panel is opened.

*/

/* per-instance C state. Add your own fields here. */
typedef struct SkeletonData
{
	int     enabled;	/* the Enable checkbox - gate your behavior on it */
	int     panelBuilt;	/* the panel is built once, when it has a path     */
	TaskObj buildTask;	/* fires one tick after creation, to build the panel */
	/* TODO: your own state (counters, handles, a TaskObj clock, ...) */
} SkeletonData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void Skeleton_BuildPanel(NodeObj instance);
static int  Skeleton_BuildTask(NodeObj instance, NodeObj data, int msgid);

/* every loadable object MUST export this - the loader dlsym's it to decide
   the module is a valid object */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Skeleton handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- your logic ----------------------------------------------------- */

/* Out is just a property. Setting it fans out to everything subscribed -
   the Out LED, and anything a flow wired to it. The write IS the event;
   there is no "out port". */
static void Skeleton_Emit(NodeObj instance, int value)
{
	SetPropStr(instance, "Out", value ? "1" : "0");
}

/* ---- action handlers (a property with an OnMsg handler acts on write) - */

/* In: a value arrived from a wired source (or a control). Do your thing.
   In is just a property NAMED In - not a special port kind. */
int Skeleton_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");
	int v;

	if (!local || message == msg_eof)
		return rtrn_handled;
	if (!local->enabled)
		return rtrn_handled;

	v = GetValueInt(data) ? 1 : 0;

	/* TODO: your real logic. Here: echo the input to Out. */
	Skeleton_Emit(instance, v);

	return rtrn_handled;
}

/* Trigger: a command. A MoButton, a Pulse, or a script all write a 1 to it
   the same way - act on 1, ignore 0 and EOF. */
int Skeleton_OnTrigger(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->enabled)
		return rtrn_handled;

	/* TODO: your command. Here: pulse Out high. */
	Skeleton_Emit(instance, 1);

	return rtrn_handled;
}

/* Enable: 1 allows operation, 0 gates it off. */
int Skeleton_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	/* mirror the port's own value WITHOUT re-firing (SetValueStr does not
	   fan out) - never SetProp a port's own name, it would shadow it */
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* Placement setup - the framework's Activate hook, run once by the build
   task so the panel comes up live. Settle initial display here. */
int Skeleton_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Skeleton_BuildPanel(instance);
	}

	/* TODO: any resting state (e.g. Out low) */
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

/* create a property that carries a handler: a write to it acts. */
static void Skeleton_Handler(NodeObj instance, char *name, char *initial, void *handler)
{
	NodeObj port;

	SetPropStr(instance, name, initial);
	port = GetPropNode(instance, name);
	SetPropLong(port, "OnMsg", (long)handler);
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	SkeletonData *local = malloc(sizeof(SkeletonData));

	local->enabled = 1;
	local->panelBuilt = 0;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Skeleton");

	/* plain data properties (displayed / read live) */
	SetPropStr(instance, "Out", "0");		/* result - the LED reflects it */
	/* TODO: SetPropStr(instance, "YourOption", "0"); ... */

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Skeleton_Activate);

	/* properties that ACT when written - each carries an OnMsg handler.
	   In / Out are ordinary properties named In / Out, not a port kind. */
	Skeleton_Handler(instance, "In",      "0", (void *)Skeleton_OnIn);
	Skeleton_Handler(instance, "Trigger", "0", (void *)Skeleton_OnTrigger);
	Skeleton_Handler(instance, "Enable",  "1", (void *)Skeleton_OnEnable);

	InitPosition(instance);

	/* SET THE VIEW'S OWN W/H HERE, before any client can subscribe. A size
	   set later (in the deferred build) shadows the W/H node the client's
	   tap is already on and never reaches it. */
	SetPropInt(instance, "W", 260);
	SetPropInt(instance, "H", 200);

	RegisterInstance(class, instance);

	/* DEFERRED BUILD: populate the panel one tick from now, after the bridge
	   has placed this instance and given it a path. Building in InstanceStart
	   is too early (no path -> controls can't be addressed). */
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)Skeleton_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control) AND seed it: hand the control the
   property's CURRENT value now, so the GUI shows it the moment the control
   is created. A plain Connect only fires on the NEXT change. */
static void Skeleton_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees).
   The framework runs from the project root, so docs are objects/<name>/. */
static char *Skeleton_ReadFile(char *path)
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

/* one control placed into a container view and wired to the widget's own
   property. The kind of wire follows the control class. */
static void Skeleton_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
						 int x, int y, int w, int h)
{
	char cpath[256], path[300];
	NodeObj c = CreateObject(container, cls);
	if (!c)
		return;

	/* name it after its property and REGISTER ITS PATH - the "has a path"
	   half of "created in a location" that CreateObject leaves to the caller */
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
		Connect(c, "Out", target, prop);		/* a command: press -> write prop */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
	{
		/* the Help box starts EMPTY; its README.md is read from disk into its
		   Value only when the Help panel is OPENED (Skeleton_OnHelpOpen). */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		Skeleton_Reflect(target, prop, c, "Value");	/* a readout: set its Value */
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);			/* the pick drives prop */
		Skeleton_Reflect(target, listprop, c, "Items");	/* options from prop+List */
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));
	}
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);		/* control edits prop */
		Skeleton_Reflect(target, prop, c, "In");	/* prop reflects into control */
	}
}

/* a sub-panel: a View put into the panel, rendering as an openable icon,
   then populated with its own controls - a view inside a view. */
static NodeObj Skeleton_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
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

/* the Help panel was OPENED: its ReservedViewOpen was delivered here (the
   open state is NOT saved). Read this widget's README.md from disk and set
   it into the Help box's Value with an update. Resolve the box BY PATH so
   the write lands on the exact node the client is subscribed to. */
int Skeleton_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;			/* only on OPEN (-> 1) */

	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = Skeleton_ReadFile("objects/skeleton/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

/* The panel: one flat table, each control tagged with the panel it lives on
   (0 = the main view itself, 1 = the Help sub-view). x/y/w/h are pixels.
   TODO: add / change controls to match your widget. */
typedef struct { char *cls, *prop; int x, y, w, h, panel; } SkelCtl;

static SkelCtl SkeletonPanel[] = {
	/* --- panel 0: the widget's own view --- */
	{ "Checkbox", "Enable",   200,  14,   8,  8, 0 },
	{ "MoButton", "Trigger",   20,  40,  60, 20, 0 },
	{ "LED",      "Out",      120,  44,  12, 12, 0 },
	/* TODO: { "Textbox", "Something", ... }, { "Dropdown", "Mode2", ... }, ... */

	/* --- panel 1: Help (loads README.md on open) --- */
	{ "Markdown", "HelpText",  10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1 },

	{ NULL, NULL, 0, 0, 0, 0, 0 }
};

/* build the panel: panel 0 goes straight into the object, the Help sub-view
   renders as an openable icon. */
static void Skeleton_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = Skeleton_SubPanel(instance, "Help", 10, 150, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED.
	   The view's open property is ReservedViewOpen (a RESERVED view name -
	   see README.md); a write to it is delivered to this handler. */
	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)Skeleton_OnHelpOpen);
	}

	for (i = 0; SkeletonPanel[i].cls; i++)
	{
		SkelCtl *t = &SkeletonPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			Skeleton_Ctl(container, instance, t->cls, t->prop, t->x, t->y, t->w, t->h);
	}
}

static int Skeleton_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		Skeleton_BuildPanel(instance);
		Skeleton_Activate(instance, msg_initialize, NULL);
	}

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	if (local)
	{
		/* stop any pending tasks before freeing local, or a still-scheduled
		   task fires later with a dangling instance pointer as its data */
		if (local->buildTask)
			RemoveTask(local->buildTask);
		/* TODO: if (local->yourTask) DeleteTask(local->yourTask); */
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Skeleton");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);		/* X/Y/W/H + the reserved view props */

	/* PUBLISH every property the outside world may see. "data" for a plain
	   property; a property that carries a handler is still "data" (the OnMsg
	   makes it act - there is no "in"/"out" direction).
	   NEVER name a data property with a reserved view name (ReservedViewMode,
	   ReservedViewOpen, ReservedViewPanelX/Y, ReservedViewResizeable) - the
	   view owns those. */
	PublishProp(ClassSelf, "Enable",   "data", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "Trigger",  "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "In",       "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "Out",      "data", PROP_LED, "0");
	PublishProp(ClassSelf, "State",    "data", PROP_LED, "1");
	/* TODO: publish your own properties, and *List properties for Dropdowns */

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

	SetName(temp, "Skeleton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "REPLACE-WITH-A-FRESH-UUID");
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
