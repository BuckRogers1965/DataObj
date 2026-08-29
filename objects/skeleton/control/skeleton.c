/*
 * Skeleton - a CONTROL: one thing on screen, with a name, a place and a size.
 *
 * A control is the smallest presented thing. It renders as an atom - a control
 * and its label, nothing else - because its class descends from Control, and
 * the client asks the engine what kind of thing arrived (the classParent it
 * carries) rather than keeping a list of names. Write one and it renders
 * correctly the first time, with no client change.
 *
 * It has no panel of its own: it is what panels are BUILT FROM. If you find
 * yourself laying other controls out inside it, you are writing a Widget.
 *
 * Descends from Control, which descends from Object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#include "control.h"

/* the browser half: show.mk turns the js and css under show/web into these
   two string literals. Nobody edits JavaScript inside quotes - you edit the
   file under show/web, never this header. */
#include "show_web.h"

typedef struct InstanceData
{
	int enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

/* whatever arrives becomes the value this control shows. No message-type
   filter, deliberately: a plain property's fan-out arrives as msg_change and a
   port's as msg_send, and a write is a write either way. */
int Skeleton_OnValue(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message;

	if (!local || !local->enabled)
		return rtrn_dropped;

	/* SetValueStr, not SetPropStr: SetPropStr drops a repeat as "no change",
	   so the same text entered twice would reach nothing subscribed. A
	   control that triggers something has to pass the second one too. */
	SetValueStr(GetPropNode(instance, "Value"), GetValueStr(data));
	SndMsg(instance, "Value", msg_send, NULL);

	return rtrn_handled;
}

/* Enable is an ordinary property, so ANYTHING can drive it through Connect() -
   a Pulse, a script, another control. It must gate everything this control
   does, on every handler, not just the obvious one. */
int Skeleton_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	/* msg_eof on an enable line means nothing - only a value does */
	if (!local || message == msg_eof)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj       instance = NewNode(INTEGER);
	NodeObj       port;
	InstanceData *local;

	(void) message; (void) data;

	local = malloc(sizeof(InstanceData));
	memset(local, 0, sizeof(InstanceData));
	local->enabled = 1;

	SetName(instance, "Skeleton");
	SetPropLong(instance, "local", (long)local);

	/* the one value this control is. A control has ONE thing it holds - if
	   you need three, that is three controls, or a Widget. */
	SetPropStr(instance, "Value", "");
	port = GetPropNode(instance, "Value");
	SetPropLong(port, "OnMsg", (long)Skeleton_OnValue);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Skeleton_OnEnable);

	/* X/Y/W/H, Container, Name, Deletable: what it takes to be placed. This
	   is the line that makes it a Control rather than a plain object. */
	InitPosition(instance);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Skeleton");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Control");

	/* the published interface: X/Y/W/H and friends, then this control's own
	   properties with the widget type each one presents as */
	PublishPosition(ClassSelf);

	/* THIS CONTROL'S OWN PRESENTATION, shipped with it: the widget type it
	   presents as, plus the browser half from show/web. A new control
	   renders correctly the first time with no change to the client. */
	PublishShow(ClassSelf, PROP_TEXTBOX, show_web_js, show_web_css);

	PublishProp(ClassSelf, "Value", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
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

	SetName(temp, "Skeleton");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "REPLACE-WITH-A-FRESH-UUID");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	/* a control needs the Control class - that is what InitPosition and
	   PublishPosition come from, and what the client reads to know this
	   renders as an atom */
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
