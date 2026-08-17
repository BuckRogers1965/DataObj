/*
 * Script - the class every scripting language host is one of.
 *
 * A host runs source in a fresh context on Activate, carries In/Out for
 * dataflow, Print for output and loud errors, and Cmd/Evt so a script is a
 * peer of the browser on the same protocol. It is a plain Object: nothing
 * puts it on a canvas - ScriptBox creates one and drives it.
 *
 * What belongs here is whatever every language does the same way, starting
 * with the runaway guard: a wall-clock budget the interpreter's interrupt
 * hook checks. QuickJS has one of its own and Lua has none, which is the
 * kind of gap a shared class closes for both at once.
 *
 * A stand-in for now - it registers the class and takes its place in the
 * chain. What this class does not answer falls through to Object.
 */

#include <stdio.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#include "sched.h"
#include "DebugPrint.h"
#define SCRIPT_IMPL
#include "script.h"

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* ------------------------------------------------------------------------
 * The common half of every language host.
 *
 * A host implements: make a context, walk the verb table registering each
 * entry, marshal its language's values to and from DataObj, run source.
 * Everything a script can DO is here, once, so two languages cannot drift
 * apart the way Lua and JSScript already had (Lua had sibget/sibset and no
 * print, JSScript had print and cmd and no siblings, and only one of them
 * had a runaway guard).
 * --------------------------------------------------------------------- */

#define SCRIPT_DEFAULT_BUDGET_MS 500

/* REAL wall clock, deliberately not the framework's GetCurrentTime: that one
   is a cache the main loop refreshes once per tick, and the whole point of
   this guard is the case where a script never gives the main loop back. A
   cached clock would never advance and the deadline would never arrive. */
static long ScriptNowUsec(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000L + tv.tv_usec;
}

/* one subscription a script made with connect(path, prop, function) */
typedef struct ScriptSub
{
	struct ScriptSub *next;
	NodeObj           self;			/* the host, so dispatch can find its state */
	long              cbHandle;		/* opaque: the host turns this back into a callable */
	char              path[300];
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	char              port[80];
} ScriptSub;

/* what Script keeps for a host, hung off the host instance as "scriptcommon" */
typedef struct ScriptCommon
{
	NodeObj    owner;			/* who created us and gets the callbacks */
	MsgId      msgBase;			/* their chosen base: answers are base + ordinal */
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	char       port[80];		/* their port */

	long       budgetMs;		/* wall clock allowed per run, 0 = unlimited */
	long       deadline;		/* micros; 0 when not running */

	void     (*invoke)(NodeObj self, long cbHandle, DataObj value);
	ScriptSub *subs;
} ScriptCommon;

static ScriptCommon *Common(NodeObj self)
{
	return self ? (ScriptCommon *) GetPropLong(self, "scriptcommon") : NULL;
}

/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
void ScriptAttach(NodeObj self, NodeObj owner, MsgId msgBase, char *port)
{
	ScriptCommon *c;

	if (!self)
		return;

	c = malloc(sizeof(ScriptCommon));
	memset(c, 0, sizeof(ScriptCommon));
	c->owner    = owner;
	c->msgBase  = msgBase;
	c->budgetMs = SCRIPT_DEFAULT_BUDGET_MS;
	snprintf(c->port, sizeof(c->port), "%s", port ? port : "");

	SetPropLong(self, "scriptcommon", (long) c);
}

void ScriptDetach(NodeObj self)
{
	ScriptCommon *c = Common(self);
	ScriptSub    *s, *next;

	if (!c)
		return;

	for (s = c->subs; s; s = next)
	{
		next = s->next;
		free(s);
	}
	free(c);
	SetPropLong(self, "scriptcommon", 0);
}

void ScriptSetInvoke(NodeObj self, void (*invoke)(NodeObj, long, DataObj))
{
	ScriptCommon *c = Common(self);

	if (c)
		c->invoke = invoke;
}

/* Print / Out / Error, as base + ordinal on the owner's own port. The owner
   chose the base, so one owner can hold several hosts and still tell them
   apart. */
void ScriptReport(NodeObj self, int ordinal, char *text)
{
	ScriptCommon *c = Common(self);
	NodeObj       chunk;

	if (!c || !c->owner || !c->port[0])
		return;

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, text ? text : "");
	DeliverMsg(c->owner, c->port, c->msgBase + ordinal, chunk);
	DelNode(chunk);
}

/* ---- the runaway guard -------------------------------------------------
   One deadline, kept here, so a host cannot forget to have one. Each
   interpreter installs its own hook (QuickJS an interrupt handler, Lua a
   debug hook) and that hook asks ScriptOverBudget. */

void ScriptSetBudget(NodeObj self, long ms)
{
	ScriptCommon *c = Common(self);

	if (c)
		c->budgetMs = ms < 0 ? 0 : ms;
}

void ScriptStartRun(NodeObj self)
{
	ScriptCommon *c = Common(self);

	if (!c)
		return;
	c->deadline = c->budgetMs ? ScriptNowUsec() + c->budgetMs * 1000L : 0;
}

int ScriptOverBudget(NodeObj self)
{
	ScriptCommon *c = Common(self);

	if (!c || !c->deadline)
		return 0;

	if (ScriptNowUsec() < c->deadline)
		return 0;

	c->deadline = 0;
	ScriptReport(self, SCRIPT_ERROR, "script ran past its time budget - stopped");
	return 1;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	/* nothing of its own yet: dropped, so the walk continues to Object */
	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Script");

	/* no InstanceStart: you instantiate a Lua or a JSScript, never a Script.
	   Which language hosts exist is now a question about this class's
	   children, so nothing needs a ScriptHost marker property. */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* the names script.h looks up. A host reaches all of this through the
	   class node - it never links against script.object. */
	SetPropLong(ClassSelf, "Attach",     (long)ScriptAttach);
	SetPropLong(ClassSelf, "Detach",     (long)ScriptDetach);
	SetPropLong(ClassSelf, "Verbs",      (long)ScriptVerbs);
	SetPropLong(ClassSelf, "SetInvoke",  (long)ScriptSetInvoke);
	SetPropLong(ClassSelf, "OverBudget", (long)ScriptOverBudget);
	SetPropLong(ClassSelf, "StartRun",   (long)ScriptStartRun);
	SetPropLong(ClassSelf, "Report",     (long)ScriptReport);
	SetPropLong(ClassSelf, "SetBudget",  (long)ScriptSetBudget);

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

	SetName(temp, "Script");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "045bb6bc-d9e4-47f6-acf3-27c6553c3ff0");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)Handle_Message);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}

/* ---- the verbs ---------------------------------------------------------
 * The engine's own verbs, not a set invented for scripting: the same calls
 * the bridge translates and the flow interpreter replays. A script is a
 * translator like they are, so it gets the same vocabulary.
 *
 * Everything takes and returns DataObj, so a script handing over a number
 * where a string is wanted simply works - conversion is the data object's
 * job, not the binding's and not the script author's.
 *
 * A host NEVER names these individually. It walks the table and registers
 * each entry through one trampoline, so a verb added here shows up in every
 * language the day it is written.
 */

/* the owner is the thing with a place in the world - a private host has no
   path of its own, so "me" and "my siblings" are the OWNER's */
static NodeObj VerbOwner(NodeObj self)
{
	ScriptCommon *c = Common(self);

	return c ? c->owner : NULL;
}

static DataObj Str(char *s)
{
	DataObj d = NewData(STRING);

	SetStr(d, s ? s : "");
	return d;
}

static char *Arg(DataObj *argv, int i)
{
	return argv && argv[i] ? GetStr(argv[i]) : "";
}

/* a sibling is a child of the owner: /Root/MyBox/Output from inside MyBox */
static NodeObj Sibling(NodeObj self, char *name)
{
	NodeObj owner = VerbOwner(self);
	char    path[400], own[300];

	if (!owner || !name || !name[0])
		return NULL;
	if (!RequirePathOf(owner, own, sizeof(own)))
		return NULL;
	snprintf(path, sizeof(path), "%s/%s", own, name);
	return RequirePath(path);
}

/* --- state ------------------------------------------------------------- */

/* Reading a property resolves the way writing one already does. A control
   points its Value at the object's property, so a read that stopped at the
   named slot would see the link rather than the data - and a script would
   get "" where it asked for a value. Writes have always gone through
   SetOrDeliverProp/ResolvePort; this is the same rule on the way out, and
   the same one the Bridge's subscribe follows. A property that owns its
   value resolves to itself, so this is what GetPropStr did plus the link. */
static char *ScriptReadProp(NodeObj inst, char *name)
{
	NodeObj owner = inst, node;

	if (!inst || !name)
		return NULL;

	node = ResolvePort(&owner, name);

	return node ? GetValueStr(node) : NULL;
}

static DataObj V_getprop(NodeObj self, DataObj *argv, long cb)
{
	NodeObj owner = VerbOwner(self);
	char   *v;
	(void) cb;
	v = owner ? ScriptReadProp(owner, Arg(argv, 0)) : NULL;
	return Str(v ? v : "");
}

static DataObj V_setprop(NodeObj self, DataObj *argv, long cb)
{
	NodeObj owner = VerbOwner(self);
	(void) cb;
	if (owner)
		SetOrDeliverProp(owner, Arg(argv, 0), Arg(argv, 1));
	return NULL;
}

static DataObj V_sibget(NodeObj self, DataObj *argv, long cb)
{
	NodeObj sib = Sibling(self, Arg(argv, 0));
	char   *v;
	(void) cb;
	v = sib ? ScriptReadProp(sib, "Value") : NULL;
	return Str(v ? v : "");
}

static DataObj V_sibset(NodeObj self, DataObj *argv, long cb)
{
	NodeObj sib = Sibling(self, Arg(argv, 0));
	(void) cb;
	if (sib)
		SetOrDeliverProp(sib, "Value", Arg(argv, 1));
	return NULL;
}

static DataObj V_pathget(NodeObj self, DataObj *argv, long cb)
{
	NodeObj inst = RequirePath(Arg(argv, 0));
	char   *v;
	(void) self; (void) cb;
	v = inst ? ScriptReadProp(inst, Arg(argv, 1)) : NULL;
	return Str(v ? v : "");
}

static DataObj V_pathset(NodeObj self, DataObj *argv, long cb)
{
	NodeObj inst = RequirePath(Arg(argv, 0));
	(void) self; (void) cb;
	if (inst)
		SetOrDeliverProp(inst, Arg(argv, 1), Arg(argv, 2));
	return NULL;
}

/* --- emitting ----------------------------------------------------------- */

static DataObj V_send(NodeObj self, DataObj *argv, long cb)
{
	(void) cb;
	ScriptReport(self, SCRIPT_OUT, Arg(argv, 0));
	return NULL;
}

static DataObj V_print(NodeObj self, DataObj *argv, long cb)
{
	(void) cb;
	ScriptReport(self, SCRIPT_PRINT, Arg(argv, 0));
	return NULL;
}

/* log() is for the AUTHOR, print() is for the widget: a line on the server's
   log tagged with whoever owns this script, rather than something a driver
   shows in an Output box. Both languages had it before there was a table -
   dropping it silently broke every generated agent, which is what a table is
   supposed to make impossible. */
static DataObj V_log(NodeObj self, DataObj *argv, long cb)
{
	NodeObj owner = VerbOwner(self);
	char   *name  = owner ? GetPropStr(owner, "Name") : NULL;
	char    line[600];

	(void) cb;

	snprintf(line, sizeof(line), "[script %s] %.400s",
			 name && name[0] ? name : "?", Arg(argv, 0));
	DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);
	return NULL;
}

/* --- composition: the engine verbs -------------------------------------- */

static DataObj V_create(NodeObj self, DataObj *argv, long cb)
{
	char   *cls = Arg(argv, 0), *path = Arg(argv, 1);
	char    cpath[300], *slash;
	NodeObj container, inst;

	(void) self; (void) cb;

	snprintf(cpath, sizeof(cpath), "%s", path);
	slash = strrchr(cpath, '/');
	if (!slash)
		return Str("");
	*slash = 0;

	container = RequirePath(cpath[0] ? cpath : "/Root");
	if (!container)
		return Str("");

	inst = CreateObject(container, cls);
	if (!inst)
		return Str("");

	/* RegisterPath names it from the path - and a Name written first would
	   strand the name it arrived with (see Widget_Create) */
	RegisterPath(path, inst);
	return Str(path);
}

static DataObj V_destroy(NodeObj self, DataObj *argv, long cb)
{
	NodeObj inst = RequirePath(Arg(argv, 0));
	(void) self; (void) cb;

	if (inst)
	{
		UnregisterPath(Arg(argv, 0));
		DeleteInstance(inst);
	}
	return NULL;
}

static DataObj V_activate(NodeObj self, DataObj *argv, long cb)
{
	NodeObj inst = RequirePath(Arg(argv, 0));
	(void) self; (void) cb;

	if (inst)
		ActivateInstance(inst);
	return NULL;
}

static DataObj V_disconnect(NodeObj self, DataObj *argv, long cb)
{
	NodeObj from = RequirePath(Arg(argv, 0));
	NodeObj to   = RequirePath(Arg(argv, 2));
	(void) self; (void) cb;

	if (from && to)
		Disconnect(from, Arg(argv, 1), to, Arg(argv, 3));
	return NULL;
}

/* --- introspection ------------------------------------------------------ */

static DataObj V_exists(NodeObj self, DataObj *argv, long cb)
{
	(void) self; (void) cb;
	return Str(ResolvePath(Arg(argv, 0)) ? "1" : "0");
}

/* --- connect: the one verb that can name a function INSIDE the script ----
 *
 * Two forms, one verb, because the engine already works this way: Connect
 * records {Instance, Port, Callback}, and a callback is exactly what a script
 * function is. So there is no separate subscribe.
 *
 *   connect(path, prop, otherpath, otherprop)   wire two other things
 *   connect(path, prop, function)               wire it to me
 *
 * The host hands over an opaque handle for the function - a LUA_NOREF, a
 * JSValue, whatever its language uses. Script stores it with the subscription
 * and hands it straight back when the message arrives; only the host knows
 * how to turn it into a call. */

static int ScriptSub_OnMsg(NodeObj instance, MsgId message, NodeObj data)
{
	ScriptSub    *s = (ScriptSub *) GetPropLong(instance, "sub");
	ScriptCommon *c;
	DataObj       value;

	(void) message;

	if (!s || !s->self)
		return rtrn_dropped;

	c = Common(s->self);
	if (!c || !c->invoke)
		return rtrn_dropped;

	value = NewData(STRING);
	SetStr(value, data ? GetValueStr(data) : "");
	c->invoke(s->self, s->cbHandle, value);
	DelData(value);

	return rtrn_handled;
}

static DataObj V_connect(NodeObj self, DataObj *argv, long cb)
{
	NodeObj       from = RequirePath(Arg(argv, 0));
	ScriptCommon *c    = Common(self);
	ScriptSub    *s;
	NodeObj       sink, to;

	if (!from)
		return NULL;

	/* four arguments and no callback: wire two other things, exactly the
	   Connect the bridge's connect command makes */
	if (!cb)
	{
		to = RequirePath(Arg(argv, 2));
		if (to)
			Connect(from, Arg(argv, 1), to, Arg(argv, 3));
		return NULL;
	}

	if (!c)
		return NULL;

	/* a function in the script: the sink is a node of ours carrying an
	   OnMsg, which is what any other sink is. Nothing special is needed
	   in the engine for a script to be a subscriber. */
	s = malloc(sizeof(ScriptSub));
	memset(s, 0, sizeof(ScriptSub));
	s->self     = self;
	s->cbHandle = cb;
	snprintf(s->path, sizeof(s->path), "%s", Arg(argv, 0));
	snprintf(s->port, sizeof(s->port), "%s", Arg(argv, 1));
	s->next = c->subs;
	c->subs = s;

	sink = NewNode(INTEGER);
	SetName(sink, "ScriptSub");
	SetPropLong(sink, "sub", (long) s);
	SetPropLong(sink, "OnMsg", (long) ScriptSub_OnMsg);
	AddProp(self, sink);

	Connect(from, Arg(argv, 1), self, "ScriptSub");
	return NULL;
}

/* ---- the table ---------------------------------------------------------
   name, DataObj argument count, whether a trailing script function follows.
   Add a verb HERE and every language has it - that is the whole point. */
static ScriptVerb Verbs[] = {
	{ "getprop",    1, 0, V_getprop    },
	{ "setprop",    2, 0, V_setprop    },
	{ "sibget",     1, 0, V_sibget     },
	{ "sibset",     2, 0, V_sibset     },
	{ "pathget",    2, 0, V_pathget    },
	{ "pathset",    3, 0, V_pathset    },

	{ "send",       1, 0, V_send       },
	{ "print",      1, 0, V_print      },
	{ "log",        1, 0, V_log        },

	{ "create",     2, 0, V_create     },
	{ "destroy",    1, 0, V_destroy    },
	{ "activate",   1, 0, V_activate   },
	{ "connect",    4, 1, V_connect    },
	{ "disconnect", 4, 0, V_disconnect },

	{ "exists",     1, 0, V_exists     },
	{ NULL,         0, 0, NULL         }
};

/* NOT HERE YET, and each for the same reason: what a script gets BACK is
   undecided, and a verb's return shape is the part that cannot be changed
   afterwards without breaking every script that used it.

     alias(path, target, targetprop)   straightforward - just not needed yet
     move(path, container)                     "
     clone(path, newpath)              returns the new path? the whole subtree?
     list(container)                   a list, in a language with no list type
                                       in its DataObj currency: newline-
                                       separated? repeated calls with an index?
                                       a callback per member?
     wires(path)                       same question, with four fields per row
     save/load/export/import           these are Serializer verbs, so Script
                                       would depend on serializer.object -
                                       fine, but decide whether a script
                                       reaching persistence is wanted at all

   The list question is the real one. Everything above returns a scalar because
   DataObj is a scalar, and the moment one verb returns a collection there has
   to be a convention for it that every language honours. Adding a verb is one
   line here; adding a CONVENTION is forever. */

ScriptVerb *ScriptVerbs(void)
{
	return Verbs;
}
