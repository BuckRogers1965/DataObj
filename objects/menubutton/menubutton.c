
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

MenuButton object: the framework's own answer to "how do we build a
menu" - modeled on the VNOS menu-button demo (objects/demo/menubutton),
brought inward the same way pulsepb.c's ControlInfo[] pattern became
ControlSpec/BuildSettingsView. Not just for our own topbar - a genuinely
standalone, reusable palette class like LED or Checkbox, so any app
built on this framework gets menus the same way it gets an LED: drag
one out, set Label and Items, wire Selected wherever it needs to go.

Three plain data properties, nothing else special: Label (the button's
own static caption, e.g. "File"), Items (a comma-separated list, e.g.
"Load,Save,Import"), Selected (whatever was last picked). Selected is
an ordinary property - it fans out to subscribers unconditionally, the
same as any other (node.c), so Connect(MenuButton1, "Selected", target,
"In") or the Bridge's own subscribe both just work, no Out port needed.

Presentation is client-side, same recursion as every other widget: a
standalone MenuButton is a button showing Label (plus ": "+Selected
once something has been picked) and a dropdown of Items - see
PROP_MENU (object.h) and web/app.js's registerWidgetAtom/topbar
rendering. The object itself has no idea it is being used as a topbar
menu versus a dropped-in palette control - Chrome (object.c) is what
marks File/Mode as chrome, not anything in this file.

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
	DebugPrint ( "MenuButton handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: whatever arrives becomes the Selected item -   */
/* no message-type filter, deliberately, same reasoning as every other   */
/* widget's OnIn (a watchable property's own fan-out arrives as          */
/* msg_change, a real port's as msg_send, and this needs to accept both) */
int MenuButton_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled)
		return rtrn_dropped;

	value = GetValueStr(data);
	SetPropStr(instance, "Selected", value ? value : "");

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int MenuButton_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int MenuButton_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	SetName(instance, "MenuButton");
	SetPropStr(instance, "Label", "Menu");
	SetPropStr(instance, "Items", "");
	SetPropStr(instance, "Selected", "");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)MenuButton_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)MenuButton_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)MenuButton_OnEnable);

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

	SetName(class, "MenuButton");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Label",    "data", PROP_LABEL, "Menu");
	PublishProp(ClassSelf, "Items",    "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Selected", "data", PROP_MENU, "");
	PublishProp(ClassSelf, "In",       "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Enable",   "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",    "data", PROP_LED, "1");

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

	SetName(temp, "MenuButton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c749");
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
