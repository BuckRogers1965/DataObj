#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "../dns/dns.h"	/* the DNS object's interface - all this panel knows of it */

/* THIS PANEL'S id for its resolver's answers. The engine sends back the id it
   was handed, so a panel holding several engines tells their replies apart in
   one handler. */
#define RESOLVER_CALLBACK	0x7000

/*

Resolver object: the DNS instrument panel, and the worked example of driving
the DNS engine. It resolves nothing itself - it CONTAINS a DNS instance and
asks it, the same shell/engine split TCPPort has with its socket.

Type a name and press Lookup, or wire a name into In: both do the same thing,
because a name arriving IS the request. The answer fills Address, and the
lamps say whether it was found. Cancel drops an answer still owed.

Dataflow: In feeds HostName, Out carries Address - so it drops into a flow as
"names in, addresses out" and needs no wiring to its own controls.

*/

/* the display-state names */
#define ST_IDLE     "IDLE"
#define ST_LOOKING  "LOOKING UP"
#define ST_FOUND    "FOUND"
#define ST_NOTFOUND "NOT FOUND"

typedef struct InstanceData
{
	int     enabled;	/* the ONLY gate on the commands */
	NodeObj inner;		/* the DNS engine this panel owns and drives */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem ResolverPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_handled;
}

/* ---- small helpers -------------------------------------------------- */

static void Resolver_SetState(NodeObj instance, char *state)
{
	SetOrDeliverProp(instance, "State", state);
}

static void Resolver_Lamps(NodeObj instance, char *busy, char *found)
{
	SetOrDeliverProp(instance, "Busy", busy);
	SetOrDeliverProp(instance, "Found", found);
}

/* one engine for this panel's life, private - no path, no container, nothing
   else can reach it. Handed this panel's own callback id and the port on this
   panel that answers land on. */
static NodeObj Resolver_NewEngine(NodeObj instance)
{
	NodeObj cls, args, engine = NULL;
	msgobj instanceStart;
	char *name;

	for (cls = FirstClass(); cls && !engine; cls = NextClass(cls))
		{
			name = GetNameStr(cls);
			if (!name || strcmp(name, "DNS"))
				continue;

			instanceStart = (msgobj) GetPropLong(cls, "InstanceStart");
			if (!instanceStart)
				break;

			args = NewNode(INTEGER);
			SetPropLong(args, "Owner", (long) instance);
			SetPropLong(args, "MsgId", RESOLVER_CALLBACK);
			SetPropStr(args, "Callback", "Answer");
			instanceStart(cls, msg_initialize, args);
			DelNode(args);

			engine = (NodeObj) GetPropLong(cls, "LastInstance");
			break;
		}

	if (!engine)
		DebugPrint("Resolver: the DNS class is not loaded",
				   __FILE__, __LINE__, ERROR);
	return engine;
}

/* ask, from wherever the name came from */
static int Resolver_Ask(NodeObj instance, InstanceData *local, char *host)
{
	NodeObj arg;

	if (!local->enabled)
	{
		DebugPrint("Resolver: disabled - the name was not looked up",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}

	if (!local->inner || !host || !host[0])
		return rtrn_handled;

	arg = NewNode(STRING);
	SetValueStr(arg, host);
	DNSLookup(local->inner, arg);
	DelNode(arg);			/* DeliverMsg is synchronous: ours to free */

	SetOrDeliverProp(instance, "Address", "");
	Resolver_SetState(instance, ST_LOOKING);
	Resolver_Lamps(instance, "1", "0");

	return rtrn_handled;
}

/* ---- the commands --------------------------------------------------- */

/* the Lookup button, and any wire pressing it */
int Resolver_OnLookup(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	/* the press, not the release: a button writes 1 then 0, and acting on
	   both asked twice - which came back as two different A records, because
	   the resolver rotates the list */
	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;

	return Resolver_Ask(instance, local, GetPropStr(instance, "HostName"));
}

/* a name arriving IS the request - the dataflow way in */
int Resolver_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *host;

	if (!local || !data || message == msg_eof)
		return rtrn_handled;

	host = GetValueStr(data);
	if (!host || !host[0])
		return rtrn_handled;

	SetOrDeliverProp(instance, "HostName", host);
	return Resolver_Ask(instance, local, host);
}

/* stop caring about the answer for whatever is in the box */
int Resolver_OnCancel(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj arg;
	char *host;

	if (!local || !local->inner || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;

	host = GetPropStr(instance, "HostName");
	if (!host || !host[0])
		return rtrn_handled;

	arg = NewNode(STRING);
	SetValueStr(arg, host);
	DNSCancel(local->inner, arg);
	DelNode(arg);

	Resolver_SetState(instance, ST_IDLE);
	Resolver_Lamps(instance, "0", "0");

	return rtrn_handled;
}

int Resolver_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message;

	if (!local)
		return rtrn_handled;

	local->enabled = data ? GetValueInt(data) : 1;

	if (!local->enabled)
	{
		Resolver_SetState(instance, ST_IDLE);
		Resolver_Lamps(instance, "0", "0");
	}

	return rtrn_handled;
}

/* The engine answering, as RESOLVER_CALLBACK - the id this panel handed it.
   The address is the message's value, empty when the name did not resolve;
   WHICH name it was for is read back with the var, exactly as dns.h says. */
int Resolver_OnAnswer(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	NodeObj ask;
	char *addr;
	char  host[MAX_HOST_SIZE];
	char  line[MAX_HOST_SIZE + 64];

	if (!local || message != RESOLVER_CALLBACK)
		return rtrn_handled;

	addr = data ? GetValueStr(data) : NULL;

	host[0] = '\0';
	if (local->inner)
	{
		ask = NewNode(STRING);
		DeliverMsg(local->inner, "Msg", DNS_HOSTNAME_VAR, ask);
		snprintf(host, sizeof(host), "%s",
				 GetValueStr(ask) ? GetValueStr(ask) : "");
		DelNode(ask);
	}

	if (addr && addr[0])
	{
		SetOrDeliverProp(instance, "Address", addr);
		snprintf(line, sizeof(line), "%s %s", ST_FOUND, host);
		Resolver_Lamps(instance, "0", "1");
	}
	else
	{
		SetOrDeliverProp(instance, "Address", "");
		snprintf(line, sizeof(line), "%s %s", ST_NOTFOUND, host);
		Resolver_Lamps(instance, "0", "0");
	}

	Resolver_SetState(instance, line);

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	if (!local)
		return rtrn_dropped;

	local->enabled = 1;
	local->inner = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "Resolver");

	/* every published control's initial value straight from the table - a
	   reactive port where the row names a handler, a plain property otherwise */
	Widget_Init(instance, ResolverPanel);

	SetPropInt(instance, "Out", 0);
	SetPropLong(instance, "local", (long)local);

	/* which control stands in for this panel at each end of a wire: names
	   in, addresses out */
	SetPropStr(instance, "ReservedIn",  "HostName");
	SetPropStr(instance, "ReservedOut", "Address");
	Widget_Port(instance, "In",     "", (void *)Resolver_OnIn);
	Widget_Port(instance, "Answer", "", (void *)Resolver_OnAnswer);

	/* one engine for this panel's life, handed this panel's callback id.
	   Making it costs nothing and starts nothing: a DNS instance that has
	   been asked for no name schedules no task. */
	local->inner = Resolver_NewEngine(instance);

	InitPosition(instance);
	Widget_MainSize(instance, ResolverPanel);

	RegisterInstance(class, instance);

	/* the panel is built one tick from now, after the bridge has given this
	   instance its path (building now would refuse - no location yet) */
	Widget_DeferBuild(instance, ResolverPanel);

	return rtrn_handled;
}

/* The whole widget in one table. Widget_Publish (ClassStart) publishes a
   property per control - widget type from the control class;
   Widget_BuildTable (deferred) lays it out. A control's w/h ARE its size. */
static WidgetItem ResolverPanel[] = {
	/* cls          prop         def        panel   x    y    w    h   label        [handler] */
	{ "View",     "Resolver",  "",          0,   0,   0, 385, 320, 0 },   /* 0: main - content 335 wide + 50 pad */
	{ "Help",     "objects/resolver/README.md", "",
	                                        0,   0,   0,   0,   0, 0 },   /* 1: help - ALWAYS second */

	{ "Checkbox", "Enable",    "1",         0, 285,  12,   8,   8, LABEL_LEFT,  (void *)Resolver_OnEnable },
	{ "TextOut",  "State",     ST_IDLE,     0,  15,  14, 180,  15, LABEL_LEFT },

	{ "Textbox",  "HostName",  "",          0,  15,  60, 250,  24, LABEL_TOP },
	{ "MoButton", "Lookup",    "0",         0, 275,  60,  60,  20, LABEL_NONE,  (void *)Resolver_OnLookup },
	{ "MoButton", "Cancel",    "0",         0, 275,  90,  60,  20, LABEL_NONE,  (void *)Resolver_OnCancel },

	{ "Textbox",  "Address",   "",          0,  15, 130, 250,  24, LABEL_TOP },

	{ "LED",      "Busy",      "0",         0,  15, 180,  10,  10, LABEL_RIGHT },
	{ "LED",      "Found",     "0",         0,  90, 180,  10,  10, LABEL_RIGHT },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */

	/* the engine is this panel's private state - no path, no container,
	   nothing else holds a reference - so nothing else will ever free it */
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

	(void) message; (void) data;

	SetName(class, "Resolver");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Widget");

	PublishPosition(ClassSelf);

	/* every on-screen control, straight from the layout table - the widget
	   type comes from each control's class, so it is never restated here */
	Widget_Publish(ClassSelf, ResolverPanel);

	/* the two with no on-screen control: the dataflow ports */
	PublishProp(ClassSelf, "In", PROP_NULL, "");
	PublishProp(ClassSelf, "Out", PROP_NULL, "");

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

	SetName(temp, "Resolver");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "0d5b7f62-8c14-49ae-b3d0-6a92e1c47f85");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "led.object", "LED", "1", "0");
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");

	/* created in code, not from the layout table */
	AddDependency(temp, "dns.object", "DNS", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
