
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Skeleton widget: a copyable template for an instrument-panel widget. Copy it
with objects/skeleton/newwidget.sh (see README.md) to start a new one.

A widget is a COMPOSITE VIEW described by ONE table (WidgetItem[]), walked three
ways by widget.h:
  - Widget_Publish (ClassStart)  - publishes a property per control row; the
                                   widget type is derived from the control class.
  - Widget_Init    (InstanceStart) - gives the instance each control's initial
                                   value; a row with a handler becomes a reactive
                                   port, one without is a plain property.
  - Widget_BuildTable / _DeferBuild / _BuildOnce - lays the controls out one tick
                                   after creation (once the instance has a path).

A row is { cls, prop, def, panel, x, y, w, h, label, handler }. handler is LAST
so plain rows just omit it. cls "View" is a panel (the first is the main view);
cls "Help" is the standard Help sub-view (prop = README path); anything else is a
control. w/h ARE the control's pixel size. Everything is a property; there is no
"in"/"out" direction. NO hardcoded help: the Help box loads README.md on open.

*/

/* per-instance C state. Add your own fields here. */
typedef struct SkeletonData
{
	int     enabled;	/* the Enable checkbox - gate your behavior on it */
	/* TODO: your own state (counters, handles, a TaskObj clock, ...) */
} SkeletonData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem SkeletonPanel[];		/* the one table; defined below */

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

/* Placement setup - the framework's Activate hook, run once by the deferred
   build so the panel comes up live. Build the panel (once) and settle here. */
int Skeleton_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, SkeletonPanel);

	/* TODO: any resting state (e.g. Out low) */
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	SkeletonData *local = malloc(sizeof(SkeletonData));

	(void) message; (void) data;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Skeleton");

	/* every control's initial value + handler, straight from the table:
	   Enable/Trigger carry a handler (a write acts), Out is plain data. */
	Widget_Init(instance, SkeletonPanel);

	/* properties with no on-screen control: In (a wire input with a handler)
	   and the lifecycle State. TODO: your own non-control ports / *List props. */
	Widget_Port(instance, "In", "0", (void *)Skeleton_OnIn);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Skeleton_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, SkeletonPanel);	/* main size before any subscribe */
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, SkeletonPanel);	/* panel built one tick from now */

	return rtrn_handled;
}

/* The whole widget in one table: the main view, the Help sub-view, and every
   control (its initial value and, for the ports, its handler).
   TODO: add / change controls to match your widget. */
static WidgetItem SkeletonPanel[] = {
	/* cls        prop        def  panel   x    y    w   h  label       [handler] */
	{ "View",     "Skeleton", "",  0,   0,   0, 260, 200, 0 },			/* 0: main */
	{ "Help",     "objects/skeleton/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",   "1", 0, 200,  14,   8,  8, LABEL_LEFT, (void *)Skeleton_OnEnable },
	{ "MoButton", "Trigger",  "0", 0,  20,  40,  60, 20, LABEL_NONE, (void *)Skeleton_OnTrigger },
	{ "LED",      "Out",      "0", 0, 120,  44,  12, 12, LABEL_NONE },
	/* TODO: { "Textbox", "Something", "", 0, x, y, w, h, LABEL_TOP, (void *)Skeleton_OnSomething }, */

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	SkeletonData *local = (SkeletonData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */
	if (local)
	{
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

	/* every control, straight from the table - the widget type comes from each
	   control's class, so it is never restated here. Everything is "data" (a
	   subscribable value); a handler makes a property ACT, it is not a direction.
	   NEVER name a data property with a reserved view name (ReservedViewMode,
	   ReservedViewOpen, ReservedViewPanelX/Y, ReservedViewResizeable). */
	Widget_Publish(ClassSelf, SkeletonPanel);

	/* the properties with no on-screen control: the wire input and the
	   lifecycle state. TODO: publish your own non-control ports / *List props. */
	PublishProp(ClassSelf, "In",    "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "State", "data", PROP_LED, "1");

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
