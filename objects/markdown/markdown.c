
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Markdown object: a rich-text presentation sink (roadmap Phase 2.7).
Whatever markdown text arrives on In (or is set as Value) is RENDERED
in the client - headings, emphasis, code, lists - instead of shown as
raw characters. The first customer is the help system: a Help button
is nothing but a View holding one of these fed a class's README. But
it is deliberately a general widget: any flow can push markdown at it
(a report, a status summary, a formatted log).

Engine-side it is the same pure display sink TextOut is - no task,
nothing to schedule, no editing. The RENDERING is presentation and
lives with the projector, keyed off PROP_MARKDOWN; the engine only
stores and fans out the text, like every other property.

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
	DebugPrint ( "Markdown handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* subscription callback: whatever arrives becomes the displayed Value - */
/* same no-filter rule as Textbox (property fan-out arrives msg_change,  */
/* a real port's traffic msg_send; EOF is ignored, a display just keeps  */
/* showing the last thing it was handed)                                  */
int Markdown_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *value;

	if (!local || !local->enabled || message == msg_eof)
		return rtrn_dropped;

	value = GetValueStr(data);
	SetPropStr(instance, "Value", value ? value : "");

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Markdown_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* nothing async here - Activate just goes live */
int Markdown_Activate(NodeObj instance, MsgId message, NodeObj data)
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
	SetName(instance, "Markdown");
	SetPropStr(instance, "Value", "");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Markdown_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Markdown_OnIn);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Markdown_OnEnable);

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

	SetName(class, "Markdown");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Value",  "data", PROP_MARKDOWN, "");
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

	SetName(temp, "Markdown");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "4c7d92e5-81f3-49b6-a2d8-5e90c3b7f614");
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
