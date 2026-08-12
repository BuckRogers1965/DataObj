#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "script.h"

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

/* the callback base WE chose - the host answers on OUR "Evt" port at
   base + SCRIPT_PRINT / SCRIPT_OUT / SCRIPT_ERROR. Ours to pick, which is
   what lets one owner hold several hosts and still tell them apart. */
#define SCRIPTBOX_CALLBACK 0x6100

typedef struct InstanceData
{
	int     active;
	int     enabled;
	NodeObj host;		/* the language host - a PRIVATE handle: no name, no
						   path, unfindable, unwireable, never serialized */
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

/* the runtime discovery: every class whose PARENT is Script, comma-joined.
   No list maintained anywhere and no marker property to remember to set -
   being a language host is now a fact about the class tree. Drop a new
   language .object in and it appears in the dropdown.                      */
static void ScriptBox_DiscoverHosts(char *out, int outlen)
{
	NodeObj lib, cls;
	int used = 0, first = 1;
	char *nm;

	out[0] = '\0';
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
		{
			char *par = GetPropStr(cls, "Parent");

			if (!par || strcmp(par, "Script"))
				continue;

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
/* Output is SET, not appended. Reading the property back and writing it plus
   the new text made every write depend on what the property already held -
   and that same property is written from the other side by the control's
   mirror, so a write coming back round grew the value again, and again. */
static void ScriptBox_Append(NodeObj instance, char *value)
{
	if (!value)
		return;

	SetPropStr(instance, "Output", value);
}

/* Everything the host says comes back HERE, on our own "Evt" port, as
   SCRIPTBOX_CALLBACK + ordinal. Not wires to the host's properties - it has
   none. One handler, and the ordinal says which kind. */
int ScriptBox_OnEvt(NodeObj instance, MsgId message, NodeObj data)
{
	char   *text = data ? GetValueStr(data) : "";

	switch (message - SCRIPTBOX_CALLBACK)
	{
		case SCRIPT_PRINT:
			ScriptBox_Append(instance, text);
			return rtrn_handled;

		case SCRIPT_ERROR:
			/* The LED IS the state: lit means the source compiled and In will
			   step it. An error means it did not, so the light goes out and
			   stepping stops - there is no second flag saying so. */
			ScriptBox_Append(instance, text);
			SetPropStr(instance, "State", "0");
			return rtrn_handled;

		case SCRIPT_OUT:
			/* Output IS the out. There is no second property to keep in step:
			   writing the value once is both what the box shows and what
			   anything downstream sees. */
			ScriptBox_Append(instance, text);
			return rtrn_handled;
	}

	return rtrn_dropped;
}

/* our In -> the host, by message. Nothing is wired to it; we hold it. */
int ScriptBox_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message;

	/* the LED is the gate: In steps what compiled, and nothing else */
	if (!local || !local->enabled || !local->host
		|| !GetPropInt(instance, "State"))
		return rtrn_dropped;

	ScriptIn(local->host, data);
	return rtrn_handled;
}

/* Build (or rebuild) the language host. It is a PRIVATE HANDLE: created
   through that class's own InstanceStart, never named, never path-registered,
   never wired. Nothing else in the session can find it, which is the point -
   the old version made an addressable child called "Inner" and then held a
   node belonging to it across its own DeleteInstance, which is a
   use-after-free ASAN caught.
   The code itself lives on US, in our Source property, so it survives the
   swap, a save, and a clone - none of which the host takes part in. */
static void ScriptBox_SwapHost(NodeObj instance, char *lang)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj       cls, args;
	msgobj        instanceStart;
	char         *src;

	if (!local || !lang || !lang[0])
		return;

	/* already running this language? The host node's own name is the answer -
	   asked of the thing itself rather than of a copy that can drift. */
	if (local->host && GetNameStr(local->host)
		&& strcmp(GetNameStr(local->host), lang) == 0)
		return;

	if (local->host)
	{
		NodeObj dying = local->host;

		/* clear it FIRST: nothing of ours may still be holding it while it
		   is being torn down */
		local->host = NULL;
		DeleteInstance(dying);
	}

	cls = FindClass(lang);
	if (!cls)
	{
		char buf[160];
		snprintf(buf, sizeof(buf), "ScriptBox: no script host class '%s'", lang);
		DebugPrint(buf, __FILE__, __LINE__, ERROR);
		SetPropStr(instance, "Output", buf);
		return;
	}

	instanceStart = (msgobj) GetPropLong(cls, "InstanceStart");
	if (!instanceStart)
	{
		DebugPrint("ScriptBox: that host class cannot make instances",
				   __FILE__, __LINE__, ERROR);
		return;
	}

	/* the callback address, chosen by us */
	args = NewNode(INTEGER);
	SetPropLong(args, "Owner", (long) instance);
	SetPropLong(args, "MsgBase", (long) SCRIPTBOX_CALLBACK);
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	SetPropStr(args, "Port", "Evt");
	instanceStart(cls, msg_initialize, args);
	DelNode(args);

	local->host = (NodeObj) GetPropLong(cls, "LastInstance");
	if (!local->host)
		return;


	/* the code carries over: it was always ours */
	src = GetPropStr(instance, "Source");
	if (src && src[0])
	{
		NodeObj text = NewNode(STRING);

		SetName(text, "Source");
		SetValueStr(text, src);
		ScriptSetSource(local->host, text);
		DelNode(text);
	}
}

/* Which language a NEW ScriptBox comes up in. DEFAULT_LANGUAGE is looked for
   in the discovered list and used if it is there; anything else falls back to
   the first entry, so a tree with no Lua still gets a working box. Note this
   MUTATES hosts (strtok), which is fine - the caller has already published
   the untouched copy. */
#define DEFAULT_LANGUAGE "Lua"

static char *ScriptBox_PickDefault(char *hosts)
{
	char *tok, *first = NULL;

	/* strtok terminates each token in place, so every pointer it hands back is
	   a NUL-terminated name inside `hosts` - no copy needed, and no returning
	   the whole remainder of the list the way strstr would */
	for (tok = strtok(hosts, ","); tok; tok = strtok(NULL, ","))
	{
		if (!first)
			first = tok;
		if (strcmp(tok, DEFAULT_LANGUAGE) == 0)
			return tok;
	}

	return first;
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

	/* The dropdown's value IS the property, and it is the only truth about
	   which language this box is in. There used to be a `local->lang` copy
	   here and the swap was gated on it differing - so any path that left the
	   copy disagreeing with the real host (a SwapHost that failed after
	   deleting the old one, a value set at InstanceStart without going
	   through this handler) left the label reading one language while another
	   one ran. Nothing is cached now: the property says what it says, and
	   SwapHost's own comparison against the live host decides whether there
	   is work to do. */
	SetValueStr(GetPropNode(instance, "Language"), lang);
	ScriptBox_SwapHost(instance, lang);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int ScriptBox_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	/* A value arrived, so act on it. This used to require msg_send, but a
	   checkbox wired to this property delivers msg_change (a property's own
	   fan-out) - so clicking Enable ran none of this and disabling did
	   nothing at all. What matters is that there IS a value, not which route
	   carried it. */
	if (!local || !data)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	/* Enable used to set this flag and nothing else looked at it on the
	   compile path, so unchecking it stopped nothing.

	   Off means: the light goes out, In stops stepping, and the host DROPS
	   what it compiled. That last part matters - without it the LED would say
	   nothing is compiled while the interpreter still held a full dictionary,
	   which is the same two-truths trap as everything else tonight. Back on is
	   a blank slate: press Activate to compile again. */
	if (!local->enabled)
	{
		SetPropStr(instance, "State", "0");
		if (local->host)
			ScriptStop(local->host);
	}

	return rtrn_handled;
}

/* Run: clear Output, hand the current Source to the inner host, run it */
int ScriptBox_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *src;
	int firstCall;

	(void) data; (void) message;

	if (!local)
		return rtrn_dropped;

	/* ActivateInstance (object.c) always calls Activate(instance,
	   msg_initialize, NULL) - every caller, every time, deferred-build's
	   own auto-call included - so message can never tell "just came up"
	   apart from "the user pressed Run"; every other object in this tree
	   gates real behavior on its OWN state flag instead (Reader/Pulse:
	   local->active), never on message. Here that flag is whether the
	   inner host exists yet: it doesn't on the very first call (the
	   deferred build coming up - build the panel and bring the host up,
	   but do not run), and does on every call after. */
	firstCall = !local->host;

	{
		char dbg[400];
		snprintf(dbg, sizeof(dbg),
				 "ScriptBox_Activate '%s': firstCall=%d host=%p enabled=%d lang='%s'",
				 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
				 firstCall, (void *) local->host, local->enabled,
				 GetPropStr(instance, "Language") ? GetPropStr(instance, "Language") : "");
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}

	/* now the box has a path (deferred build / activation), build the panel and
	   bring the inner host up - a sub-object needs the box's OWN path first, so
	   this cannot happen in InstanceStart (the path is set after it returns) */
	Widget_BuildOnce(instance, ScriptBoxPanel);
	if (!local->host)
		ScriptBox_SwapHost(instance, GetPropStr(instance, "Language"));

	if (firstCall || !local->host)
		return rtrn_handled;

	/* Output is NOT cleared here. Clearing it and then running put two values
	   into circulation, "" and whatever the script printed - and because the
	   control's re-announce is queued, the stale one lands after the box has
	   moved on, writes it back, and the two values regenerate each other
	   forever. On screen that is the output blinking. A run simply sets the
	   value it produces. */

	/* ACTIVATE IS THE COMPILE BUTTON. It hands the current text over and has
	   the host compile it - that is all. Stepping is the In line: each arrival
	   there runs one step against what was compiled. Splitting the two is what
	   lets a stateful language work here at all; re-evaluating the source per
	   step re-allocated every definition and overflowed the interpreter's heap
	   after a dozen presses.

	   State is the LED: lit means compiled and ready to step. */
	if (!local->enabled)
		return rtrn_handled;

	/* Nothing to compile is not a compile. An empty box gets activated at
	   startup - the palette builds one of everything and activates it - and
	   lighting the LED there said "compiled and ready to step" about a box
	   with no source in it. */
	src = GetPropStr(instance, "Source");
	if (!src || !src[0])
		return rtrn_handled;
	{
		NodeObj text = NewNode(STRING);

		SetName(text, "Source");
		SetValueStr(text, src ? src : "");
		ScriptSetSource(local->host, text);
		DelNode(text);
	}
	/* Lit BEFORE the run, and the return value checked. The host reports a
	   compile error synchronously while running - ScriptBox_OnEvt puts the
	   light out - so setting it green afterwards painted over the failure and
	   a broken source came up green. And DeliverMsg returns 0 when the host
	   never took the message at all, which is not a compile either.

	   The client colours this LED straight from the value: 0 grey, 1 yellow,
	   2 green (web/style.css). */
	SetPropStr(instance, "State", "2");
	if (!ScriptRun(local->host))
		SetPropStr(instance, "State", "0");

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
	{ "Button",   "Run",      "0", 0,  15, 235,  70,  24, LABEL_NONE,  (void *)ScriptBox_Activate },
	{ "Checkbox", "In",       "0", 0, 110, 240,   9,   9, LABEL_RIGHT, (void *)ScriptBox_OnIn },
	{ "LED",      "State",    "0", 0, 165, 240,  12,  12, LABEL_NONE },
	{ "Textbox",  "Output",   "",  0,  15, 270, 400, 120, LABEL_NONE },

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
	local->host = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "ScriptBox");

	/* every control's value + handler from the table (Language/In/Enable carry
	   a handler; Source/Output/State/Out are plain data; Run triggers Activate) */
	Widget_Init(instance, ScriptBoxPanel);

	/* the discovered hosts fill the Language menu. The default is CHOSEN, not
	   whichever host happened to register first: registration order is scan
	   order, and AddChild prepends, so "the first one" silently became a
	   different language every time a host was added to the tree. Set WITHOUT
	   re-firing the port (SetPropStr on its own name would shadow it). */
	ScriptBox_DiscoverHosts(hosts, sizeof(hosts));
	SetPropStr(instance, "LanguageList", hosts);
	first = ScriptBox_PickDefault(hosts);
	SetValueStr(GetPropNode(instance, "Language"), first ? first : "");

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)ScriptBox_Activate);

	/* internal wiring ports for the inner host's Print and Out - not published */
	/* one port for everything the host says back - the ordinal distinguishes
	   Print from Out from Error, so there is nothing to keep in step */
	Widget_Port(instance, "Evt", "0", (void *)ScriptBox_OnEvt);

	/* which control stands in for this widget at each end of a wire, so it
	   can be wired at its closed icon: what arrives is In, what it says is
	   Out - both ordinary properties it already has. Same declaration
	   tcpport makes for TxData/RxData. */
	SetPropStr(instance, "ReservedIn",  "In");
	SetPropStr(instance, "ReservedOut", "Output");

	(void) first;

	InitPosition(instance);
	Widget_MainSize(instance, ScriptBoxPanel);
	RegisterInstance(class, instance);

	/* the deferred build calls Activate(msg_initialize) once the box has a path -
	   that is where the panel AND the inner host come up (both need the path, so
	   NEITHER can be created here in InstanceStart). msg_initialize does not run
	   the script; only the Run button (msg_send) does. */
	/* QUIET: build the panel, do NOT call Activate. Creating a ScriptBox must
	   not compile or run anything - the palette builds one of everything at
	   boot and a catalog entry is looked at, not run (BuildPalette's own
	   contract, control.c). The loud variant called Activate one tick after
	   creation, which is what compiled and lit every box at startup. */
	Widget_DeferBuildQuiet(instance, ScriptBoxPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		/* the host IS ours to delete: it is a private handle, not a
		   path-registered child, so no subtree snapshot knows about it and
		   nothing else can be holding it. That is the whole difference from
		   the old addressable "Inner", which could be deleted twice - once
		   here and once by the caller's own subtree walk. */
		if (local->host)
		{
			NodeObj dying = local->host;

			local->host = NULL;
			DeleteInstance(dying);
		}
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

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every control, from the table (Language menu, Source/Output boxes, the
	   visible In toggle, Out readout - widget type from each control's class) */
	Widget_Publish(ClassSelf, ScriptBoxPanel);
	PublishProp(ClassSelf, "LanguageList", PROP_NULL, "");	/* menu options, no control */

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
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	/* needs at least one ScriptHost class to be useful, but does not fail  */
	/* to load without one - the dropdown is simply empty                    */
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "button.object", "Button", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "dropdown.object", "Dropdown", "1", "0");
	AddDependency(temp, "led.object", "LED", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
