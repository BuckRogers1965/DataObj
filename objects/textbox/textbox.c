
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "control.h"
#include "show_web.h"

/*

Textbox object: a genuinely standalone class, pulled out of the old
objects/widget/widget.c grab-bag the same way LED and Button already
were. See objects/led/led.c's doc comment for the reasoning.

A pure display/input sink: no task, nothing to schedule. Whatever
arrives on In becomes the displayed Value - Connect(SomeSource,
"SomeProp", Textbox1, "In") is enough, since every property fans out
to whatever's Connect()ed to it (WatchableProp, object.c) the same way
a real Out port does. There is deliberately no Out port: reaching back
out to an arbitrary target property is ConnectToProperty's job
(object.c), not this object's.

*/

typedef struct InstanceData
{
	int active;
	int enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Textbox handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: whatever arrives becomes the displayed Value - */
/* no message-type filter, deliberately: a watchable property's own fan- */
/* out (PropertyChanged) arrives as msg_change, a real port's as msg_send */
int Textbox_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj announce;
	char *value;

	if (!local || !local->enabled)
		return rtrn_dropped;

	value = GetValueStr(data);

	/* A write is a write. SetPropStr would apply its change test and drop
	   a repeat, so the same text typed twice reached nothing subscribed -
	   and for a box that TRIGGERS something (a packet to send, a string to
	   re-encode) a repeated value is not a non-event. Store it without the
	   test, then announce it, so every write is exactly one delivery. */
	SetValueStr(GetPropNode(instance, "Value"), value ? value : "");

	announce = NewNode(STRING);
	SetName(announce, "Value");
	SetValueStr(announce, value ? value : "");
	SndMsg(instance, "Value", msg_send, announce);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Textbox_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int Textbox_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Textbox");
	SetPropStr(instance, "Value", "");
	WatchableProp(instance, "Value");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Textbox_Activate);

	/* Value IS the control: a wire into it writes it, the hand writes
	   it, and the write fans out. There is no second copy to keep in
	   step and nothing to deliver that can overwrite it. */
	port = GetPropNode(instance, "Value");
	SetPropLong(port, "OnMsg", (long)Textbox_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Textbox_OnEnable);


	InitPosition(instance);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
		free(local);

	return rtrn_handled;
}

/* A GESTURE THIS CLASS OFFERS. GUI_Format is a mask the browser applies to
   what this box shows and validates what is typed against - an annotation
   on the data, which is why it lives on the instance like any other
   property. Typing nothing removes it. */
static int Textbox_ClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	char *name, *value;

	if (message != msg_gesture || !data)
		return rtrn_unhandled;

	name = GetPropStr(data, "Name");
	if (!name || strncmp(name, "Format", 6))
		return rtrn_unhandled;

	if (GetPropStr(data, "Query"))
	{
		char *had = GetPropStr(instance, "GUI_Format");

		SetPropStr(data, "Value", had ? had : "");
		SetPropStr(data, "Prop", "GUI_Format");
		return rtrn_handled;
	}

	value = GetPropStr(data, "Value");

	/* EMPTY MEANS REMOVE, not "set to nothing". A property that exists
	   holding "" is still an annotation somebody has to reason about; the
	   delete half of the lifecycle takes it away. */
	if (!value || !value[0])
	{
		NodeObj prop = GetPropNode(instance, "GUI_Format");

		if (prop)
		{
			RemoveProp(instance, prop);
			DelNode(prop);
		}
	}
	else
		SetPropStr(instance, "GUI_Format", value);

	{
		char dbg[200];
		snprintf(dbg, sizeof(dbg), "Textbox: format on '%s' is now '%s'",
				 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
				 value ? value : "");
		DebugPrint(dbg, __FILE__, __LINE__, OBJMSGHANDLING);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Textbox");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Control");

	PublishPosition(ClassSelf);

	/* how it shows itself, carried by the class - see show/web/ */
	PublishShow(ClassSelf, PROP_TEXTBOX, show_web_js, show_web_css);

	PublishProp(ClassSelf, "Value", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State", PROP_LED, "1");

	/* ON THE CLASS NODE - PuntToClass walks class nodes, and a ClassMsg
	   left on the library node is somewhere the walk never looks. */
	SetPropLong(ClassSelf, "ClassMsg", (long)Textbox_ClassMsg);
	PublishGestures(ClassSelf, "Format...");

	/* the suggestions, in the same companion-list convention a Dropdown's
	   options already use (Language -> LanguageList). The engine says what
	   the common patterns are; the browser only offers them. */
	/* "what it is = the mask", so the list reads like English and picking
	   one fills in the pattern. Entries are comma separated (the same
	   Items convention), so a mask must not contain a comma - none of
	   these do. */
	SetPropStr(ClassSelf, "FormatList",
			   "Phone=(###) ###-####,"
			   "Phone with dashes=###-###-####,"
			   "SSN=###-##-####,"
			   "Date MM/DD/YYYY=##/##/####,"
			   "Date YYYY-MM-DD=####-##-##,"
			   "Time HH:MM=##:##,"
			   "Time HH:MM:SS=##:##:##,"
			   "Zip=#####,"
			   "Zip+4=#####-####,"
			   "Currency=$###.##,"
			   "Percent=##.##,"
			   "Credit card=#### #### #### ####,"
			   "Licence plate=AA-####");

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

	SetName(temp, "Textbox");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c742");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "control.object", "Control", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
