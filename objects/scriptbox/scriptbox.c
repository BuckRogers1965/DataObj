#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

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

	if (!local)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	if (!local->inner)
		ScriptBox_SwapInner(instance, GetPropStr(instance, "Language"));
	if (!local->inner)
		return rtrn_handled;

	SetPropStr(instance, "Output", "");

	src = GetPropStr(instance, "Source");
	SetOrDeliverProp(local->inner, "Source", src ? src : "");
	ActivateInstance(local->inner);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));
	char hosts[512];
	char *first;

	local->active = 0;
	local->enabled = 1;
	local->inner = NULL;
	local->lang[0] = '\0';

	instance = NewNode(INTEGER);
	SetName(instance, "ScriptBox");

	ScriptBox_DiscoverHosts(hosts, sizeof(hosts));

	SetPropStr(instance, "LanguageList", hosts);

	/* default to the first discovered host */
	first = strtok(hosts, ",");
	SetPropStr(instance, "Language", first ? first : "");

	SetPropStr(instance, "Source", "");
	SetPropStr(instance, "Output", "");
	SetPropInt(instance, "Out", 0);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)ScriptBox_Activate);

	/* Language is a port: the dropdown's Selected wires here, and the      */
	/* handler swaps the inner host (the Enable pattern)                     */
	port = GetPropNode(instance, "Language");
	SetPropLong(port, "OnMsg", (long)ScriptBox_OnLanguage);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)ScriptBox_OnIn);

	/* unpublished wiring ports for the inner host's Print and Out */
	SetPropInt(instance, "InnerPrint", 0);
	port = GetPropNode(instance, "InnerPrint");
	SetPropLong(port, "OnMsg", (long)ScriptBox_OnInnerPrint);

	SetPropInt(instance, "InnerOut", 0);
	port = GetPropNode(instance, "InnerOut");
	SetPropLong(port, "OnMsg", (long)ScriptBox_OnInnerOut);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)ScriptBox_OnEnable);

	InitPosition(instance);

	RegisterInstance(class, instance);

	/* the inner host exists from birth so a fresh ScriptBox is runnable    */
	/* immediately (the dropdown just re-selects what's already there)       */
	if (first)
		ScriptBox_SwapInner(instance, GetPropStr(instance, "Language"));

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

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
	NodeObj entry;

	SetName(class, "ScriptBox");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* the dropdown, then the two big boxes, then the dataflow ports.       */
	/* LanguageList is the menu's options (companion to the PROP_MENU       */
	/* Language, the same convention the client reads for any menu prop)    */
	/* "data" not "in": a read (subscribe) comes back as property-changed   */
	/* so the dropdown hydrates, while the OnMsg handler on the port node    */
	/* (InstanceStart) still fires on a write to swap the inner host - a     */
	/* property can be both watchable data and an active port                */
	PublishProp(ClassSelf, "Language",     "data", PROP_MENU, "");
	PublishProp(ClassSelf, "LanguageList", "data", PROP_NULL, "");
	/* the object declares its own box sizes: Rows/Cols annotations on the  */
	/* published entry (properties are nodes, so an entry can be annotated  */
	/* without any new mechanism). The declared size is FIXED - text beyond  */
	/* it scrolls inside the box; boxes do not resize by content             */
	entry = PublishProp(ClassSelf, "Source",       "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 12);
	SetPropInt(entry, "Cols", 48);
	entry = PublishProp(ClassSelf, "Output",       "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 8);
	SetPropInt(entry, "Cols", 48);
	/* In is a VISIBLE control (the Enable pattern): a checkbox on the      */
	/* panel writes 1/0 straight into the script's In, so an oninput        */
	/* script can be poked by hand - the icon's In dot and this checkbox     */
	/* are the same port                                                      */
	PublishProp(ClassSelf, "In",           "in",   PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "Out",          "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Enable",       "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",        "data", PROP_LED, "1");

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
