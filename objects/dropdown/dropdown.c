
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Dropdown object: the dropdown PRIMITIVE - a real control class like
Textbox or Checkbox. Items (comma-separated) are the choices, Value is
the current selection (rendered as a native select, Widget=PROP_MENU),
In sets the selection like any control's In. Wire its Value anywhere -
a ScriptBox's Language, a Filter's Mode - and setting Items IS setting
the menu; no client-side list anywhere.

A pure display/input sink: no task, nothing to schedule. Whatever
arrives on In becomes the displayed Value - Connect(SomeSource,
"SomeProp", Dropdown1, "In") is enough, since every property fans out
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
	DebugPrint ( "Dropdown handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: whatever arrives becomes the displayed Value - */
/* no message-type filter, deliberately: a watchable property's own fan- */
/* out (PropertyChanged) arrives as msg_change, a real port's as msg_send */
int Dropdown_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled)
		return rtrn_dropped;

	value = GetValueStr(data);
	SetPropStr(instance, "Value", value ? value : "");

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Dropdown_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int Dropdown_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	SetName(instance, "Dropdown");
	SetPropStr(instance, "Value", "");
	SetPropStr(instance, "Items", "");
	WatchableProp(instance, "Value");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Dropdown_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Dropdown_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Dropdown_OnEnable);

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

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Dropdown");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Value",  "data", PROP_MENU, "");
	PublishProp(ClassSelf, "Items",  "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "In",     "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Enable", "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",  "data", PROP_LED, "1");

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

	SetName(temp, "Dropdown");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "e91b4f27-8a53-4c06-b7d9-2f48a1c6e035");
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
