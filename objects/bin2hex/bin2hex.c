
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Bin2Hex widget: dumps its Input bytes as uppercase hex. A one-way transform,
no clock: whatever is written to Input is hex-dumped into Output whenever
Input or Enable changes.

Everything is a property; there are no in/out ports. Input and Output are
plain properties named that. Default input connection is Input, default
output connection is Output. This direction always produces printable hex,
so its Output is plain ASCII.

*/

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates the transform */
	int     panelBuilt;	/* the panel is built once, when the object has a path */
	TaskObj buildTask;	/* fires one tick after creation, to build the panel  */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void Bin2Hex_BuildPanel(NodeObj instance);
static int  Bin2Hex_BuildTask(NodeObj instance, NodeObj data, int msgid);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Bin2Hex handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the transform -------------------------------------------------- */

/* format the input bytes as uppercase hex - two digits per byte, a space
   between bytes, a newline every 16. Every byte gets a trailing separator.
   returns a malloc'd NUL-terminated string; caller frees. */
static char *Bin2Hex_Encode(const char *in)
{
	size_t len = in ? strlen(in) : 0;
	char  *out = malloc(len * 3 + 1);	/* 2 hex + 1 separator per byte, +NUL */
	size_t i, o = 0;

	if (!out)
		return NULL;

	for (i = 0; i < len; i++)
	{
		o += sprintf(out + o, "%2.2X", (unsigned char)in[i]);
		out[o++] = ((i + 1) & 15) ? ' ' : '\n';	/* 16 bytes to a line */
	}
	out[o] = '\0';
	return out;
}

/* read Input live and, if Enabled, write the hex dump to Output. Output is a
   plain property; setting it fans out to the Output textbox and anything a
   flow wired to it. A disabled dumper leaves Output untouched. */
static void Bin2Hex_Recompute(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *in, *out;

	if (!local || !local->enabled)
		return;

	in = GetPropStr(instance, "Input");
	out = Bin2Hex_Encode(in ? in : "");
	SetPropStr(instance, "Output", out ? out : "");
	if (out)
		free(out);
}

/* ---- action handlers (a property with an OnMsg handler acts on write) - */

/* Input: new text arrived (typed, or wired from a source). Mirror it onto
   the port WITHOUT re-firing, then dump. */
int Bin2Hex_OnInput(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;

	SetValueStr(GetPropNode(instance, "Input"), GetValueStr(data));
	Bin2Hex_Recompute(instance);
	return rtrn_handled;
}

/* Enable: 1 allows the transform, 0 gates it off. */
int Bin2Hex_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	Bin2Hex_Recompute(instance);		/* re-enabling refreshes Output */
	return rtrn_handled;
}

/* Placement setup - the framework's Activate hook, run once by the build
   task so the panel comes up live and Output reflects the initial Input. */
int Bin2Hex_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Bin2Hex_BuildPanel(instance);
	}

	Bin2Hex_Recompute(instance);
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

/* create a property that carries a handler: a write to it acts. */
static void Bin2Hex_Port(NodeObj instance, char *name, char *initial, void *handler)
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
	local->panelBuilt = 0;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Bin2Hex");

	/* the text in and the hex out - plain data properties; Output's
	   subscribers (the Output box, downstream wires) see every write. */
	SetPropStr(instance, "Input", "");
	SetPropStr(instance, "Output", "");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Bin2Hex_Activate);

	/* the properties that ACT when written - each carries a handler.
	   Input / Enable are ordinary properties named that. */
	Bin2Hex_Port(instance, "Input",  "",  (void *)Bin2Hex_OnInput);
	Bin2Hex_Port(instance, "Enable", "1", (void *)Bin2Hex_OnEnable);

	InitPosition(instance);

	/* the view's OWN size, set here before any client can subscribe - two
	   stacked text boxes, the Enable checkbox, and the Help icon. */
	SetPropInt(instance, "W", 300);
	SetPropInt(instance, "H", 310);

	RegisterInstance(class, instance);

	/* deferred build: populate the panel one tick from now, after the bridge
	   has given this instance its path. */
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)Bin2Hex_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* wire a reflect (a property -> a control) AND seed it: hand the control the
   property's CURRENT value now, so the GUI shows it the moment the control is
   created. A plain Connect only fires on the NEXT change. */
static void Bin2Hex_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

/* read a whole file into a malloc'd, NUL-terminated string (caller frees).
   The framework runs from the project root, so docs are objects/<name>/. */
static char *Bin2Hex_ReadFile(char *path)
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
static void Bin2Hex_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
						int x, int y, int w, int h, int rows, int cols)
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

	if (strcmp(cls, "Textbox") == 0 && rows > 0 && cols > 0)
	{
		SetPropInt(c, "Rows", rows);
		SetPropInt(c, "Cols", cols);
	}

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);		/* a command: press -> write prop */
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
	{
		/* the Help box starts EMPTY; its README.md is read from disk into its
		   Value only when the Help panel is OPENED (Bin2Hex_OnHelpOpen). */
	}
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		Bin2Hex_Reflect(target, prop, c, "Value");	/* a readout: set its Value */
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);		/* control edits prop */
		Bin2Hex_Reflect(target, prop, c, "In");	/* prop reflects into control */
	}
}

/* a sub-panel: a View put into the panel, rendering as an openable icon,
   then populated with its own controls - a view inside a view. */
static NodeObj Bin2Hex_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
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
   it into the Help box's Value. Resolve the box BY PATH so the write lands on
   the exact node the client is subscribed to. */
int Bin2Hex_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
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

	md = Bin2Hex_ReadFile("objects/bin2hex/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);

	return rtrn_handled;
}

/* The panel: one flat table, every control tagged with the panel it lives on
   - 0 = the main view (the object itself), 1 = Help. */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } B2HCtl;

static B2HCtl Bin2HexPanel[] = {
	/* --- panel 0: Binary to Hex (the object's own view) --- */
	{ "Checkbox", "Enable",   210,  14,   9,  9, 0,  0,  0 },
	{ "Textbox",  "Input",     15,  40, 270, 90, 0,  5, 40 },
	{ "Textbox",  "Output",    15, 150, 270, 90, 0,  5, 40 },

	/* --- panel 1: Help (loads README.md on open) --- */
	{ "Markdown", "HelpText",  10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

/* build the panel: panel 0 goes straight into the object, the Help sub-view
   renders as an openable icon. */
static void Bin2Hex_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = Bin2Hex_SubPanel(instance, "Help", 10, 248, HELP_W, HELP_H);

	/* load the README into the Help box when the Help panel is OPENED. */
	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)Bin2Hex_OnHelpOpen);
	}

	for (i = 0; Bin2HexPanel[i].cls; i++)
	{
		B2HCtl *t = &Bin2HexPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			Bin2Hex_Ctl(container, instance, t->cls, t->prop,
						t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

static int Bin2Hex_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		Bin2Hex_BuildPanel(instance);
		Bin2Hex_Activate(instance, msg_initialize, NULL);
	}

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		/* stop any pending task before freeing local, or it fires later with
		   a dangling instance pointer as its data */
		if (local->buildTask)
			RemoveTask(local->buildTask);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	NodeObj entry;

	SetName(class, "Bin2Hex");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);		/* X/Y/W/H + the reserved view props */

	/* PUBLISH every property the outside world may see. Everything is "data";
	   Input/Enable carry a handler (OnMsg) so a write acts, but they are
	   ordinary data properties - there is no "in"/"out" direction. */
	PublishProp(ClassSelf, "Enable", "data", PROP_CHECKBOX, "1");

	entry = PublishProp(ClassSelf, "Input", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 5);
	SetPropInt(entry, "Cols", 40);

	entry = PublishProp(ClassSelf, "Output", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 5);
	SetPropInt(entry, "Cols", 40);

	PublishProp(ClassSelf, "State", "data", PROP_LED, "1");

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

	SetName(temp, "Bin2Hex");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "30a16bef-19b0-4aac-ab09-cabbbe0addcc");
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
