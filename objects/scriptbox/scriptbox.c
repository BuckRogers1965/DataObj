#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

ScriptBox object: the script WIDGET - the shell a user actually edits
code in. It holds no interpreter of its own; it CONTAINS an instance of
a language host (JSScript, Lua's Script, any class marked ScriptHost=1)
and drives it. Its panel (the engine's auto-built internals view) shows:

    Language   a dropdown of every registered script host (PROP_MENU),
               its companion LanguageList carrying the discovered names
    Source     the text box the code is edited in
    Output     a big text box the script's print()/errors flow into
    Run        Activate - hands Source to the inner host and runs it

Picking a Language SWAPS the inner host instance (the code carries
over). This is composition, not a new mechanism: the inner host is an
ordinary instance created with CreateObject and wired with Connect(),
exactly as a user would wire two objects - which is why Lua's Script
works as an inner language with ZERO changes to script.c.

Dataflow: ScriptBox is itself a flow object - its In passes through to
the inner host's In, and the inner host's Out passes back out
ScriptBox's Out, so a ScriptBox drops into a dataflow like anything
else, its chosen language invisible from outside.

*/

typedef struct InstanceData
{
	int     active;
	int     enabled;
	NodeObj inner;		/* the language-host instance we drive */
	char    lang[64];	/* the class name of `inner`           */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem ScriptBoxPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "ScriptBox handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* the runtime discovery: every class carrying ScriptHost=1, comma-joined  */
/* - no list maintained anywhere, drop a new language .object in and it     */
/* appears in the dropdown                                                   */
static void ScriptBox_DiscoverHosts(char *out, int outlen)
{
	NodeObj lib, cls;
	int used = 0, first = 1;
	char *nm;

	out[0] = '\0';
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
			if (GetPropInt(cls, "ScriptHost"))
			{
				nm = GetNameStr(cls);
				if (!nm)
					continue;
				used = (int) strlen(out);
				snprintf(out + used, outlen - used, "%s%s", first ? "" : ",", nm);
				first = 0;
			}
}

/* append one line to the Output box - a script that emits several times   */
/* accumulates, rather than each line overwriting the last                  */
static void ScriptBox_Append(NodeObj instance, char *value)
{
	char *cur, *buf;
	int len;

	if (!value)
		return;

	cur = GetPropStr(instance, "Output");
	len = (int) strlen(cur ? cur : "") + (int) strlen(value) + 2;
	buf = malloc(len);
	snprintf(buf, len, "%s%s\n", (cur && cur[0]) ? cur : "", value);
	SetPropStr(instance, "Output", buf);
	free(buf);
}

/* inner host print()/errors -> Output box */
int ScriptBox_OnInnerPrint(NodeObj instance, MsgId message, NodeObj data)
{
	if (message != msg_eof)
		ScriptBox_Append(instance, data ? GetValueStr(data) : NULL);
	return rtrn_handled;
}

/* inner host Out -> Output box AND forwarded out our own Out (a fresh      */
/* copy - the original belongs to the inner's queued delivery, Filter's     */
/* rule). So the Output box shows output whatever port a language uses      */
/* (JS print()->Print, Lua send()->Out), and ScriptBox stays a composable   */
/* flow object whose Out carries the script's dataflow output.              */
int ScriptBox_OnInnerOut(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj copy;

	if (message == msg_eof)
		return rtrn_handled;

	ScriptBox_Append(instance, data ? GetValueStr(data) : NULL);

	copy = NewNode(STRING);
	SetName(copy, "Data");
	SetValueStr(copy, data ? GetValueStr(data) : "");
	SndMsg(instance, "Out", message, copy);
	return rtrn_handled;
}

/* our In -> inner host In: deliver straight to the inner's own handler */
int ScriptBox_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->enabled || !local->inner)
		return rtrn_dropped;

	DeliverMsg(local->inner, "In", message, data);
	return rtrn_handled;
}

/* build (or rebuild) the inner host of class `lang` and wire it to us */
static void ScriptBox_SwapInner(NodeObj instance, char *lang)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !lang || !lang[0])
		return;

	/* the old host and every wire on its ports die together - the         */
	/* registry-wide subscription scrub (DeleteInstance) also removes the   */
	/* subscription our own In made onto it                                 */
	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}

	/* the host belongs to this ScriptBox - created inside it */
	local->inner = CreateObject(instance, lang);
	if (!local->inner)
	{
		char buf[160];
		snprintf(buf, sizeof(buf), "ScriptBox: no script host class '%s'", lang);
		DebugPrint(buf, __FILE__, __LINE__, ERROR);
		SetPropStr(instance, "Output", buf);
		return;
	}

	snprintf(local->lang, sizeof(local->lang), "%s", lang);

	/* the inner host's output and dataflow-out come back to us; our In     */
	/* reaches its In through ScriptBox_OnIn (a handler, so it survives      */
	/* every swap without re-wiring)                                         */
	Connect(local->inner, "Print", instance, "InnerPrint");
	Connect(local->inner, "Out",   instance, "InnerOut");
}

/* the Language dropdown drove a new value in: swap the inner host */
int ScriptBox_OnLanguage(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *lang;

	if (!local || message == msg_eof)
		return rtrn_handled;

	lang = data ? GetValueStr(data) : NULL;
	if (!lang || !lang[0])
		return rtrn_handled;

	SetValueStr(GetPropNode(instance, "Language"), lang);

	if (strcmp(lang, local->lang) != 0)
		ScriptBox_SwapInner(instance, lang);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int ScriptBox_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	return rtrn_handled;
}

/* Run: clear Output, hand the current Source to the inner host, run it */
int ScriptBox_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *src;

	(void) data;

	if (!local)
		return rtrn_dropped;

	/* now the box has a path (deferred build / activation), build the panel and
	   bring the inner host up - a sub-object needs the box's OWN path first, so
	   this cannot happen in InstanceStart (the path is set after it returns) */
	Widget_BuildOnce(instance, ScriptBoxPanel);
	if (!local->inner)
		ScriptBox_SwapInner(instance, GetPropStr(instance, "Language"));

	/* msg_initialize is placement / flow activation: come up ready, do NOT run.
	   Run (the button) arrives as msg_send - only then hand Source to the host. */
	if (message == msg_initialize || !local->inner)
		return rtrn_handled;

	local->active = 1;
	SetPropInt(instance, "State", Running);
	SetPropStr(instance, "Output", "");

	src = GetPropStr(instance, "Source");
	SetOrDeliverProp(local->inner, "Source", src ? src : "");
	ActivateInstance(local->inner);

	return rtrn_handled;
}

/* The whole panel in one table: the Language menu, the Source box, Run (=
   Activate), the visible In toggle, the Output box and the Out readout. Uses
   the QUIET deferred build - placing a ScriptBox does NOT run the script. */
static WidgetItem ScriptBoxPanel[] = {
	/* cls        prop        def  panel   x    y    w    h  label        [handler] */
	{ "View",     "ScriptBox","",  0,   0,   0, 440, 490, 0 },			/* 0: main */
	{ "Help",     "objects/scriptbox/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",   "1", 0, 410,  12,   9,   9, LABEL_LEFT,  (void *)ScriptBox_OnEnable },
	{ "Dropdown", "Language", "",  0,  15,  12, 150,  20, LABEL_NONE,  (void *)ScriptBox_OnLanguage },
	{ "Textbox",  "Source",   "",  0,  15,  45, 400, 180, LABEL_NONE },
	{ "Button",   "Run",      "",  0,  15, 235,  70,  24, LABEL_NONE },
	{ "Checkbox", "In",       "0", 0, 110, 240,   9,   9, LABEL_RIGHT, (void *)ScriptBox_OnIn },
	{ "LED",      "State",    "1", 0, 165, 240,  12,  12, LABEL_NONE },
	{ "Textbox",  "Output",   "",  0,  15, 270, 400, 120, LABEL_NONE },
	{ "TextOut",  "Out",      "",  0,  15, 400, 400,  20, LABEL_LEFT },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));
	char hosts[512];
	char *first;

	(void) message; (void) data;

	local->active = 0;
	local->enabled = 1;
	local->inner = NULL;
	local->lang[0] = '\0';

	instance = NewNode(INTEGER);
	SetName(instance, "ScriptBox");

	/* every control's value + handler from the table (Language/In/Enable carry
	   a handler; Source/Output/State/Out are plain data; Run triggers Activate) */
	Widget_Init(instance, ScriptBoxPanel);

	/* the discovered hosts fill the Language menu; default to the first, set
	   WITHOUT re-firing the port (SetPropStr on its own name would shadow it) */
	ScriptBox_DiscoverHosts(hosts, sizeof(hosts));
	SetPropStr(instance, "LanguageList", hosts);
	first = strtok(hosts, ",");
	SetValueStr(GetPropNode(instance, "Language"), first ? first : "");

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)ScriptBox_Activate);

	/* internal wiring ports for the inner host's Print and Out - not published */
	Widget_Port(instance, "InnerPrint", "0", (void *)ScriptBox_OnInnerPrint);
	Widget_Port(instance, "InnerOut",   "0", (void *)ScriptBox_OnInnerOut);

	(void) first;

	InitPosition(instance);
	Widget_MainSize(instance, ScriptBoxPanel);
	RegisterInstance(class, instance);

	/* the deferred build calls Activate(msg_initialize) once the box has a path -
	   that is where the panel AND the inner host come up (both need the path, so
	   NEITHER can be created here in InstanceStart). msg_initialize does not run
	   the script; only the Run button (msg_send) does. */
	Widget_DeferBuild(instance, ScriptBoxPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		if (local->inner)
			DeleteInstance(local->inner);
		free(local);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "ScriptBox");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (Language menu, Source/Output boxes, the
	   visible In toggle, Out readout - widget type from each control's class) */
	Widget_Publish(ClassSelf, ScriptBoxPanel);
	PublishProp(ClassSelf, "LanguageList", "data", PROP_NULL, "");	/* menu options, no control */

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

	SetName(temp, "ScriptBox");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "a5e2f7c1-3b84-4d69-9f02-7c61e8d4a903");
	SetPropStr(temp, "Version", "1.0");
	/* needs at least one ScriptHost class to be useful, but does not fail  */
	/* to load without one - the dropdown is simply empty                    */
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
