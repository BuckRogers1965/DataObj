#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Sum: adds up whatever is wired into it.

There is no list of inputs and nothing declares how many there are. A wire
records itself on BOTH ends - the source keeps a "Subscriber" saying who to
deliver to, the sink keeps a "Subscription" saying who it is listening to
(AddSubscription, object.c) - so this widget's inputs ARE its subscriptions.
Press Sum and it walks its own Input property's Subscription records and
reads what each source holds at that moment.

That is why pressing the button is the whole mechanism: it does not
accumulate, it does not cache, and it does not care when the values
arrived. It reads what is there now.

*/

typedef struct SumData
{
	int enabled;
} SumData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem SumPanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Sum handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_unhandled;
}

int Sum_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	SumData *local = (SumData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* The work. Walk what Input is listening to and add up what is there now. */
static void Sum_Compute(NodeObj instance)
{
	NodeObj  port, rec, owner, src;
	char     out[64], dbg[300];
	long     total = 0;
	int      inputs = 0;

	port = GetPropNode(instance, "Input");
	if (!port)
		return;

	for (rec = GetNextProp(port); rec; rec = GetNextSibling(rec))
	{
		if (!CmpName(rec, "Subscription"))
			continue;

		owner = (NodeObj) GetPropLong(rec, "Instance");
		src   = owner ? GetPropNode(owner, GetPropStr(rec, "Port")) : NULL;
		if (!src)
			continue;

		total += GetValueLong(src);
		inputs++;

		snprintf(dbg, sizeof(dbg), "Sum: '%s'.%s = %ld",
				 GetPropStr(owner, "Name") ? GetPropStr(owner, "Name") : "?",
				 GetPropStr(rec, "Port") ? GetPropStr(rec, "Port") : "?",
				 GetValueLong(src));
		DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
	}

	/* NOTHING WIRED IN IS ZERO. The sum of no inputs is nought, and it has
	   to say so the moment the last wire is cut - a total left over from
	   when something was connected is a lie about what is connected now. */

	snprintf(out, sizeof(out), "%ld", total);
	SetOrDeliverProp(instance, "Output", out);

	snprintf(dbg, sizeof(dbg), "Sum: %d input(s) -> %s", inputs, out);
	DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
}

/* the button. Nothing else produces an output unless Auto says so. */
int Sum_OnSum(NodeObj instance, MsgId message, NodeObj data)
{
	SumData *local = (SumData *)GetPropLong(instance, "local");

	if (message == msg_eof || !data || !GetValueInt(data))
		return rtrn_handled;			/* the press, not the release */
	if (local && !local->enabled)
		return rtrn_handled;

	Sum_Compute(instance);
	return rtrn_handled;
}

/* Something wired in delivered a value. Store it, and total up only if Auto
   is on - otherwise this widget stays quiet until the button is pressed,
   which is the default. */
int Sum_OnInput(NodeObj instance, MsgId message, NodeObj data)
{
	SumData *local = (SumData *)GetPropLong(instance, "local");
	NodeObj  port = GetPropNode(instance, "Input");

	if (message == msg_eof || !data)
		return rtrn_handled;
	if (local && !local->enabled)
		return rtrn_handled;

	/* stored on the node, not through SetPropStr: this IS Input's own
	   handler, and writing the property from inside it would fan out and
	   arrive back here */
	if (port)
		SetValueStr(port, GetValueStr(data) ? GetValueStr(data) : "");

	if (GetPropInt(instance, "Auto"))
		Sum_Compute(instance);

	return rtrn_handled;
}

/* the wiring changed - something was plugged in or cut. In Auto that is
   exactly as much a reason to re-total as a value arriving: what changed
   is WHICH inputs there are. */
int Sum_OnRewired(NodeObj instance, MsgId message, NodeObj data)
{
	SumData *local = (SumData *)GetPropLong(instance, "local");

	if (message == msg_eof || !data)
		return rtrn_handled;
	if (local && !local->enabled)
		return rtrn_handled;

	if (GetPropInt(instance, "Auto"))
		Sum_Compute(instance);

	return rtrn_handled;
}

int Sum_OnAuto(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof || !data)
		return rtrn_handled;

	SetPropInt(instance, "Auto", GetValueInt(data) ? 1 : 0);
	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	SumData *local = malloc(sizeof(SumData));

	(void) message;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Sum");

	Widget_Init(instance, SumPanel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);

	/* WHAT THE ICON STANDS FOR. A wire dropped on the widget itself names
	   no property, and Connect then asks for these - so connecting to the
	   icon lands on Input, and connecting from it takes Output, without
	   opening the panel to find a box. */
	/* A PROPERTY'S OWN HANDLER DOES NOT FIRE ON A PLAIN WRITE, and Connect
	   announces by writing Wiring - so subscribe to it, the same way a
	   RadioGroup subscribes to its own LastMember. */
	SetPropInt(instance, "Wiring", 0);
	SetPropStr(instance, "Rewired", "0");
	SetPropLong(GetPropNode(instance, "Rewired"), "OnMsg", (long)Sum_OnRewired);
	Connect(instance, "Wiring", instance, "Rewired");

	SetPropStr(instance, "ReservedIn",  "Input");
	SetPropStr(instance, "ReservedOut", "Output");

	/* NO Activate. A widget does not start; it answers what arrives. */

	InitPosition(instance);
	Widget_MainSize(instance, SumPanel);
	RegisterInstance(class, instance);

	Widget_Place(instance, data, SumPanel);

	return rtrn_handled;
}

/* Laid out once. Input over Output; Enable over Input at its right edge,
   Auto to Enable's left - both inside the width the boxes bound, so
   nothing sits at the panel's rim. */
static WidgetItem SumPanel[] = {
	/* cls        prop      def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Sum",    "",  0,   0,   0, 270, 220, 0 },
	{ "Help",     "objects/sum/README.md", "", 0, 0, 0, 0, 0, 0 },

	{ "Textbox",  "Input",  "",  0,  20,  30, 200, 24, LABEL_BOTTOM, (void *)Sum_OnInput },
	{ "Textbox",  "Output", "",  0,  20,  80, 200, 24, LABEL_BOTTOM },

	/* over Input, at its right - the checkbox's right edge is Input's right
	   edge (20 + 200 = 220), so neither reaches the panel's rim. Auto sits
	   to Enable's left. */
	{ "Checkbox", "Enable", "1", 0, 181,  14,   9,  9, LABEL_LEFT, (void *)Sum_OnEnable },
	{ "Checkbox", "Auto",   "0", 0, 125,  14,   9,  9, LABEL_LEFT, (void *)Sum_OnAuto },

	/* beside the help icon, which Widget_AddHelp puts at the bottom left */
	{ "MoButton", "Sum",    "0", 0,  70, 140,  50, 20, LABEL_NONE, (void *)Sum_OnSum },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	SumData *local = (SumData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Sum");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);
	Widget_Publish(ClassSelf, SumPanel);
	PublishProp(ClassSelf, "State", PROP_LED, "1");

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Sum");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "f221f011-96a0-478b-a50c-190a20455b8c");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
