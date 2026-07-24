
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

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
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem Bin2HexPanel[];

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

	Widget_BuildOnce(instance, Bin2HexPanel);
	Bin2Hex_Recompute(instance);
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Bin2Hex");

	/* every control's initial value and handler, from the table: Input and
	   Enable carry a handler (a write acts), Output is plain data. */
	Widget_Init(instance, Bin2HexPanel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Bin2Hex_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, Bin2HexPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, Bin2HexPanel);

	return rtrn_handled;
}

/* The whole widget in one table: the main view, the Help sub-view, and every
   control (with its initial value and, for the ports, its handler). */
static WidgetItem Bin2HexPanel[] = {
	/* cls        prop       def  panel   x    y    w   h  label       [handler] */
	{ "View",     "Bin2Hex", "",  0,   0,   0, 380, 360, 0 },			/* 0: main */
	{ "Help",     "objects/bin2hex/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",  "1", 0, 348,  14,   9,  9, LABEL_LEFT, (void *)Bin2Hex_OnEnable },
	{ "Textbox",  "Input",   "",  0,  15,  40, 350, 110, LABEL_TOP,  (void *)Bin2Hex_OnInput },
	{ "Textbox",  "Output",  "",  0,  15, 190, 350, 110, LABEL_TOP },

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

	SetName(class, "Bin2Hex");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);		/* X/Y/W/H + the reserved view props */

	/* every control, straight from the table (widget type from the class) */
	Widget_Publish(ClassSelf, Bin2HexPanel);
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
