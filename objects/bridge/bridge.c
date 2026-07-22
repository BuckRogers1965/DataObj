
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/*

Bridge object: a JSON control protocol over whatever transport is wired
to its In/Out ports (raw TCP today, the same way Http sits over TCP -
Connect(Tcp, "Out", Bridge, "In"); Connect(Bridge, "Out", Tcp, "In") -
WebSocket later without the Bridge itself changing at all).

Commands in, one JSON object per message:

    {"cmd":"create-instance","class":"Reader","as":"Reader1"}
    {"cmd":"create-instance","class":"LED","as":"_LED1","hidden":"1"}
    {"cmd":"connect","from":"Reader1","fromPort":"Out","to":"Writer1","toPort":"In"}
    {"cmd":"disconnect","from":"Reader1","fromPort":"Out","to":"Writer1","toPort":"In"}
    {"cmd":"set-property","instance":"Reader1","prop":"Filename","value":"test.txt"}
    {"cmd":"activate","instance":"Reader1"}
    {"cmd":"subscribe","instance":"Reader1","port":"Out"}
    {"cmd":"login","user":"jim","token":"..."}
    {"cmd":"list-instances"}

This is a veneer, exactly as the roadmap says: every command is a direct
call to CreateObject/Connect/SetPropStr/ActivateInstance, the same
functions CreateTestApp and the Flow* recording API already call. The
only thing the Bridge adds is a persistent alias table (instance name ->
live NodeObj pointer) so a remote client can refer to instances by name
across separate messages, the same trick RunFlow uses for one flow
script - shared for this Bridge instance's whole lifetime now, not just
one connection's (see the doc comment above InstanceData).

There is no separate "what classes exist" protocol. list-instances is
the one thing a connecting client asks for: the view. That view is two
groups of real instances, both walked and reported the exact same way -
Root (the live session's own instances, this Bridge's alias table) and
Palette (one inert instance of every registered class, GetPalette() in
object.c, built once at startup) - each instance-created event says
which group it came from. A palette entry is not a description of a
class, it IS an instance, discoverable and inspectable the same way
anything else in the tree is; a client wanting to place a new Reader
still sends create-instance as always; it just no longer needs a
separate round trip first to learn what a Reader looks like, since every
instance-created event (Root or Palette) already carries its class's
full published Interface inline.

connect reaches ANY property on any instance - a compiled-in port like
Reader's "Enable", a plain data property like Filename, or an
instance's Activate - one verb, one engine call (Connect(), object.c,
universal default delivery). bind-property and bind-activate survive
only as dispatch synonyms for it, so recorded flows replay; new
clients should send connect. A wire made or removed is announced with
a connected/disconnected event to every connection viewing either
endpoint's container - the client draws and erases from those events
only, never from its own gesture.

Events out, on success or failure:

    {"event":"instance-created","instance":"Reader1","class":"Reader","parent":"Root","interface":{...Interface node, verbatim...}}
    {"event":"property-changed","instance":"Reader1","port":"State","value":"2"}
    {"event":"message-flowed","instance":"Reader1","port":"Out","value":"..."}
    {"event":"connected","from":"Reader1","fromPort":"Out","to":"Writer1","toPort":"In"}
    {"event":"disconnected","from":"Reader1","fromPort":"Out","to":"Writer1","toPort":"In"}
    {"event":"logged-in","user":"jim"}
    {"event":"instances-done"}
    {"event":"error","cmd":"connect","message":"..."}

subscribe attaches a "tap" - a bare node carrying an OnMsg handler, not
a registered class instance - to any port or property, exactly like
Connect() would for a real object. The tap calls SndMsg on this Bridge
instance's own Out directly rather than needing its own transport
wiring, reusing whatever this Bridge is already connected to (raw TCP
or a WebSocket). Whether a subscription reports property-changed or
message-flowed is read off the source class's own published interface
(Phase 1.4) - a "data" direction is a property, "in"/"out" is a port -
so the client never has to say which kind it's asking for.

login is a no-op unless RequireAuth is set to "1" on this instance
(default "0", so every Bridge before this keeps working unauthenticated).
When it is set, nothing but login works until AuthenticateUser (object.c,
checked against Main/Users/<name>'s Token) succeeds. Main must also be
set on this instance - a raw NodeObj pointer, the same cross-reference
convention "local" already uses - or there is no Users tree to check
against. TLS is not implemented: token auth first is exactly what the
roadmap asks for at this stage, and TLS means linking OpenSSL, a real
new dependency this build does not have yet.

Multiple viewers, one shared session: TCP now services any number of
simultaneous connections at once (see tcp.c), each message tagged with a
Conn id, so this Bridge instance is no longer "one connection's worth of
state that resets when that peer leaves" - the alias table and container
are genuinely shared for the Bridge instance's whole lifetime, the same
object graph no matter which of possibly several connected clients is
looking at it. Only two things stay legitimately per-peer: RequireAuth's
login state (one client logging in must not authenticate every other
viewer), tracked per Conn the same way Router/WebSocket track their own
per-connection state, and where a REPLY goes - Bridge_OnIn remembers the
Conn a command arrived on for the duration of handling it, and anything
that is a direct answer to that command (an error, "logged-in", the
palette dump) is tagged back to just that Conn. Anything that reflects a
change to the shared graph itself (instance-created, and every
subscription tap's property-changed/message-flowed) is left untagged -
Conn 0, TCP's "broadcast to everyone" sentinel - so every connected
viewer sees it, which is the whole point of watching a live session
together.

*/

typedef struct InstanceData
{
	NodeObj container;	/* passed to CreateObject; currently decorative    */
	NodeObj connAuth;	/* Conn id -> authenticated (0/1), see GetConnState/SetConnState */
	NodeObj connViews;	/* Conn id -> (NodeObj) table of container keys this conn is viewing - the GUI is an alias, it only receives events about what it looked at */
	NodeObj flow;		/* recorded session: every mutating command, verbatim, in arrival order */
	long    replyConn;	/* Conn the command currently being handled arrived on, 0 = none/broadcast */
	int     active;
	int     enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Bridge handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/*
 * One self-contained event per instance: alias, class name, which group
 * it belongs to ("Palette", the one-instance-per-class catalog, or
 * "Root", a live session object - see GetPalette/BuildPalette in
 * object.c), and the class's published Interface (Widget/Direction/
 * Default for every property and port) embedded inline. A client never
 * needs a separate "what does this class look like" round trip - the
 * same reasoning that made the palette real instances instead of a
 * class-description protocol in the first place means an instance's own
 * event is a complete enough answer on its own.
 *
 * connId 0 broadcasts (a genuinely new instance via create-instance,
 * everyone watching the shared view should see it appear); a specific
 * Conn targets one client (replaying the palette and the existing
 * session back to a client that just connected, see
 * Bridge_ListInstances - the rest of the shared view already saw these
 * the first time and doesn't need them again).
 *
 * Fields are user/class-supplied (a client picks its own alias names),
 * so they go through JsonEscapeStr rather than a raw %s - an alias with
 * a quote in it would otherwise break the event's own JSON.
 *
 * hidden marks an instance that exists purely as plumbing behind some
 * other instance's control (the LED/Textbox/Button a composite object's
 * card wraps its own properties in - see web/app.js's widget-recursion
 * comment) rather than something a client should ever render a card of
 * its own for. It has to be carried as real, persistent instance state
 * (not just client-side bookkeeping of what a session created) because
 * list-instances replays this same instance to clients that never saw
 * it get created and have no other way to know it is not first-class.
 *
 * container carries the instance's current Container value inline, so
 * the client can place the element in its real parent on first render
 * instead of defaulting to the top-level canvas and waiting on a
 * separate Container subscribe reply to correct it - that round trip is
 * what let dropped/delayed events show a stray element sitting in the
 * root (see web/app.js's placeInContainer doc comment; the burst of
 * ~20 Palette instances replayed to a fresh connection is exactly the
 * case where that gap was visible). The client still subscribes to
 * Container for any later move; this only fixes the first paint.
 */
/* the GUI is one big alias onto the session: a connection only ever      */
/* receives events about containers it is actually looking at. Looking    */
/* is recorded the moment a conn asks list-instances for a container.     */
/* Nothing is broadcast blind.                                             */
/*                                                                          */
/* The key is the container path plus a "|" terminator: CmpName (node.c)   */
/* matches over the STORED name's length, so a bare "/" (the root key)     */
/* would prefix-match every path, and "/Root/View_1" would prefix-match    */
/* "/Root/View_10". The terminator makes every key match exactly itself.   */
static char *Bridge_ViewKey(char *container, char *buf, int len)
{
	snprintf(buf, len, "%s|", (container && container[0]) ? container : "/");
	return buf;
}

static void Bridge_MarkViewing(InstanceData *local, long connId, char *container)
{
	NodeObj table;
	char key[300];

	if (!connId)
		return;

	table = (NodeObj) GetConnState(local->connViews, connId);
	if (!table)
	{
		table = NewNode(INTEGER);
		SetConnState(local->connViews, connId, (long) table);
	}
	SetPropInt(table, Bridge_ViewKey(container, key, sizeof(key)), 1);
}

/* deliver one already-built event to every connection viewing container */
static void Bridge_SendEventScoped(NodeObj instance, InstanceData *local, char *json, char *container)
{
	NodeObj entry, table, chunk;
	char keybuf[300];
	char *key = Bridge_ViewKey(container, keybuf, sizeof(keybuf));
	long connId;

	for (entry = GetNextProp(local->connViews); entry; entry = GetNextSibling(entry))
	{
		table = (NodeObj) GetValueLong(entry);
		if (!table || !GetPropInt(table, key))
			continue;
		connId = atol(GetNameStr(entry));
		if (!connId)
			continue;

		chunk = NewNode(STRING);
		SetName(chunk, "Event");
		SetValueStr(chunk, json);
		SetPropLong(chunk, "Conn", connId);
		SndMsg(instance, "Out", msg_send, chunk);
	}
}

void Bridge_InstanceEvent(NodeObj instance, InstanceData *local, char *alias, char *className, NodeObj classNode, char *parent, char *container, int hidden, long connId)
{
	NodeObj interface, chunk;
	char *escAlias, *escClass, *escParent, *escContainer, *interfaceText, *buf;
	int bufLen;

	interface = classNode ? GetClassInterface(classNode) : NULL;
	interfaceText = interface ? NodeToText(interface) : strdup("null");

	escAlias     = JsonEscapeStr(alias ? alias : "");
	escClass     = JsonEscapeStr(className ? className : "");
	escParent    = JsonEscapeStr(parent ? parent : "");
	escContainer = JsonEscapeStr(container ? container : "");

	bufLen = (int) strlen(escAlias) + (int) strlen(escClass) + (int) strlen(escParent) + (int) strlen(escContainer) + (int) strlen(interfaceText) + 160;
	buf = malloc(bufLen);
	snprintf(buf, bufLen, "{\"event\":\"instance-created\",\"instance\":%s,\"class\":%s,\"parent\":%s,\"container\":%s,\"hidden\":%s,\"interface\":%s}",
			 escAlias, escClass, escParent, escContainer, hidden ? "true" : "false", interfaceText);

	free(escAlias);
	free(escClass);
	free(escParent);
	free(escContainer);
	free(interfaceText);

	if (connId)
	{
		/* a targeted replay (list-instances) - just the conn that asked */
		chunk = NewNode(STRING);
		SetName(chunk, "Event");
		SetValueStr(chunk, buf);
		SetPropLong(chunk, "Conn", connId);
		SndMsg(instance, "Out", msg_send, chunk);
	}
	else
	{
		/* a live creation - only connections viewing its container care */
		Bridge_SendEventScoped(instance, local, buf, container);
	}

	free(buf);
}

/* targeted, not broadcast - only the client whose command failed needs   */
/* to see it, reads its own replyConn off "local" so every existing call  */
/* site (already inside a Bridge_OnIn dispatch) needs no signature change */
void Bridge_Error(NodeObj instance, char *cmd, char *message)
{
	char buf[512];
	char *escCmd, *escMessage;
	NodeObj chunk;
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	/* an error is sent to the client AND logged on the server - a command  */
	/* that fails (an unresolved subscribe, a name collision, a refused     */
	/* delete) must never be silent in the log. ERROR prints at every       */
	/* verbose level (threshold 0, DebugPrint.c)                            */
	snprintf(buf, sizeof(buf), "BRIDGE ERROR: '%s' - %s", cmd ? cmd : "?", message ? message : "?");
	DebugPrint(buf, __FILE__, __LINE__, ERROR);

	escCmd = JsonEscapeStr(cmd ? cmd : "");
	escMessage = JsonEscapeStr(message ? message : "");

	snprintf(buf, sizeof(buf), "{\"event\":\"error\",\"cmd\":%s,\"message\":%s}",
			 escCmd, escMessage);

	free(escCmd);
	free(escMessage);

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, buf);
	if (local)
		SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/*
 * A client-supplied alias can name a Root instance (this session's own -
 * the Palette's bootstrap instances are seeded straight into local->
 * aliases at InstanceStart, below, so they're ordinary Root instances
 * from here on, not a separate category) or a Chrome entry (the topbar's
 * File/Mode menus, which really are app-level UI, not something a user
 * composes with - see Chrome's own doc comment). One resolver, used
 * everywhere an alias comes in off the wire, instead of patching each
 * command's own local->aliases lookup one at a time.
 */
static NodeObj Bridge_ResolveAlias(InstanceData *local, char *alias)
{
	NodeObj inst;

	if (!alias)
		return NULL;

	/* the ENGINE's path index (object.c, on the namespace trie) - one   */
	/* resolver shared by every translator, not a per-bridge table       */
	inst = ResolvePath(alias);
	if (inst)
		return inst;

	return (NodeObj) GetPropLong(GetChrome(), alias);
}

/*
 * The other direction of Bridge_ResolveAlias - given an instance, what is
 * its CURRENT alias right now. Needed because an alias is a full path
 * (/Root/Palette/Reader, see Bridge_Set's Container handling) that changes
 * whenever the instance moves to a different Container - a live tap
 * (Bridge_TapOnIn) can't just cache the alias it was created with, or its
 * events would keep reporting a stale path forever after a move. No table
 * or scan: the path is DERIVED from the Name and Container the instance
 * carries (PathOfInstance, object.c), verified by resolving back. A small
 * ring of static buffers keeps the returned strings valid across the
 * couple of concurrent uses an event formatter needs (the fabric is
 * single-threaded; this is reuse, not races).
 */
static char *Bridge_AliasForInstance(InstanceData *local, NodeObj inst)
{
	static char bufs[4][300];
	static int rot = 0;
	NodeObj entry;
	char *buf;

	if (!local || !inst)
		return NULL;

	rot = (rot + 1) & 3;
	buf = bufs[rot];
	if (PathOfInstance(inst, buf, sizeof(bufs[0])))
		return buf;

	/* chrome instances go by their short well-known names */
	for (entry = GetNextProp(GetChrome()); entry; entry = GetNextSibling(entry))
		if ((NodeObj) GetValueLong(entry) == inst)
			return GetNameStr(entry);

	return NULL;
}

/* the Name property mirrors the alias's basename - what a thing is       */
/* called is just one of its properties, set here whenever a registration */
/* or rename decides the alias                                             */
static void Bridge_SetNameFromAlias(NodeObj inst, char *alias)
{
	char *slash = strrchr(alias, '/');

	SetOrDeliverProp(inst, "Name", slash ? slash + 1 : alias);
}

/* an unused session name like <prefix>/<Base>_N - server-generated, since */
/* a deep clone names things no client asked for individually              */
static void Bridge_FreshAlias(InstanceData *local, char *prefix, char *base, char *out, int outlen)
{
	int n;

	for (n = 1; n < 100000; n++)
	{
		snprintf(out, outlen, "%s/%s_%d", (prefix && prefix[0]) ? prefix : "/Root", base, n);
		if (!ResolvePath(out))
			return;
	}
}

/* "hidden":true on the command marks an instance as plumbing rather than */
/* something first-class - see the doc comment on Bridge_InstanceEvent -  */
/* carried as real state on the instance itself (a leading underscore, by */
/* the same convention "local" already uses for non-published bookkeeping */
/* properties) so it survives to be read back correctly by list-instances */
/* no matter which client, or how much later, ends up replaying it.       */
void Bridge_Create(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *classname, *alias, *container, *x, *y;
	char fresh[256];
	int hidden;
	NodeObj inst;

	classname = GetPropStr(command, "class");
	alias     = GetPropStr(command, "as");
	container = GetPropStr(command, "container");
	x         = GetPropStr(command, "x");
	y         = GetPropStr(command, "y");
	hidden    = GetPropInt(command, "hidden") != 0;

	if (!classname || !classname[0])
	{
		Bridge_Error(instance, "create-instance", "class is required");
		return;
	}

	{
		/* create it where the command said, not in "nowhere" - the old
		   local->container was NULL and documented as decorative.
		   NO container means the ROOT VIEW, and it means that the whole
		   way down: the instance is placed there and its creation event
		   is scoped there. Placing it in "" instead left the event
		   addressed to a container nobody is viewing, so the client that
		   asked for it never heard back. */
		NodeObj home;

		if (!container || !container[0])
			container = "/Root";

		home = ResolvePath(container);
		if (!home) {
			Bridge_Error(instance, "create-instance", "unknown container");
			return;
		}
		inst = CreateObject(home, classname);
	}
	if (!inst)
	{
		Bridge_Error(instance, "create-instance", "class not found");
		return;
	}

	if (hidden)
		SetPropInt(inst, "_Hidden", 1);

	/* atomic birth: placed in its container at its position in this one  */
	/* command - same contract create-alias and clone-instance already    */
	/* keep. PlaceInstance (object.c) is the shared engine call.           */
	PlaceInstance(inst, container, x, y);

	/* the server names things; a client-supplied "as" is honored when    */
	/* given (flow replay, scripts), otherwise minted where it lives.      */
	/* A TAKEN "as" also mints fresh: honoring it would shadow the live    */
	/* entry in the alias table - two instances answering to one name,     */
	/* every binding through it dead (what Import into a session that      */
	/* already had the name used to do). The final name is stamped back    */
	/* into the command so the flow log records a fully-determined birth.  */
	if (!alias || !alias[0] || ResolvePath(alias))
	{
		Bridge_FreshAlias(local, container, classname, fresh, sizeof(fresh));
		alias = fresh;
		SetPropStr(command, "as", alias);
	}

	RegisterPath(alias, inst);
	Bridge_SetNameFromAlias(inst, alias);
	Bridge_InstanceEvent(instance, local, alias, classname, GetParent(inst), "Root", GetPropStr(inst, "Container"), hidden, 0);
}

/*
 * {"cmd":"create-alias","of":"Pulse1","prop":"Interval","as":"Speed1"}
 * - a real Alias instance whose `prop` is a node-level link to the
 * original's (LinkProperty, object.c): reads, writes, wiring, and
 * subscriptions through the alias all land on the original. Target/
 * TargetProp are ordinary watchable properties saying what it stands
 * for; Widget/Label/position are the alias's own presentation.
 * Recorded in the flow log by name like everything else, so panels
 * built out of aliases save and load.
 */
void Bridge_CreateAlias(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *of, *prop, *alias, *container, *x, *y;
	char fresh[256];
	int hidden;
	NodeObj target, inst;

	of        = GetPropStr(command, "of");
	prop      = GetPropStr(command, "prop");
	alias     = GetPropStr(command, "as");
	container = GetPropStr(command, "container");
	x         = GetPropStr(command, "x");
	y         = GetPropStr(command, "y");
	hidden    = GetPropInt(command, "hidden") != 0;

	target = Bridge_ResolveAlias(local, of);

	if (!target || !prop || !prop[0])
	{
		Bridge_Error(instance, "create-alias", "of and prop are required");
		return;
	}

	{
		NodeObj home = (container && container[0]) ? ResolvePath(container) : ResolvePath("/Root");
		if (!home) {
			Bridge_Error(instance, "create-alias", "unknown container");
			return;
		}
		inst = CreateObject(home, "Alias");
	}
	if (!inst)
	{
		Bridge_Error(instance, "create-alias", "Alias class not found");
		return;
	}

	/* the link lives in the alias's own "Value" slot - its Name/Container */
	/* /X/Y stay its own, whatever property of the target it stands for    */
	if (!LinkPropertyAs(inst, "Value", target, prop))
	{
		DeleteInstance(inst);
		Bridge_Error(instance, "create-alias", "no such property on the target");
		return;
	}

	/* record the FINAL original, not whatever happened to be dragged:    */
	/* aliasing an alias collapses to the original at the link level, and  */
	/* events always speak the original's name - so Target/TargetProp     */
	/* have to name that same thing, or a client subscribing "to the      */
	/* alias's target" would key its control on a name no event carries    */
	{
		NodeObj owner = target;
		NodeObj node, pub;
		char *realName;

		node = ResolvePort(&owner, prop);
		realName = Bridge_AliasForInstance(local, owner);
		if (realName)
			of = realName;
		if (node)
			prop = GetNameStr(node);

		/* presentation default, stamped by the ENGINE at birth: what the  */
		/* final owner's class published for this property (Widget type and */
		/* Direction). The client renders the alias's Widget property - it  */
		/* never consults the class Interface to deduce a control            */
		/* (readmefirst repair #2). Unpublished properties stay unstamped    */
		/* and render as the plain-textbox fallback.                          */
		pub = InterfacePropForInstance(owner, prop);
		if (pub)
		{
			SetPropInt(inst, "Widget", GetPropInt(pub, "Widget"));
			SetPropStr(inst, "Direction", GetPropStr(pub, "Direction"));
		}
	}

	SetPropStr(inst, "Target", of);
	SetPropStr(inst, "TargetProp", prop);

	/* the flow log records the resolved fact: an alias created THROUGH    */
	/* another alias (a panel member's doorway slot) collapses to the       */
	/* final original above, and only the original's name is replayable     */
	SetPropStr(command, "of", of);
	SetPropStr(command, "prop", prop);

	/* atomic birth: named IN its container with its position, in this    */
	/* one command - never born at root and then moved, which raced every */
	/* client that addressed it by its seconds-old birth name              */
	PlaceInstance(inst, container, x, y);

	if (hidden)
		SetPropInt(inst, "_Hidden", 1);

	/* the server names things; a client-supplied "as" is honored when   */
	/* given (scripts), otherwise - or when the name is already taken,    */
	/* see Bridge_Create - minted where it lives, and stamped back into   */
	/* the command for a fully-determined replay                           */
	if (!alias || !alias[0] || ResolvePath(alias))
	{
		Bridge_FreshAlias(local, container, "Alias", fresh, sizeof(fresh));
		alias = fresh;
		SetPropStr(command, "as", alias);
	}

	RegisterPath(alias, inst);
	Bridge_SetNameFromAlias(inst, alias);
	Bridge_InstanceEvent(instance, local, alias, "Alias", GetParent(inst), "Root", GetPropStr(inst, "Container"), hidden, 0);

	{
		char dbg[600];
		snprintf(dbg, sizeof(dbg), "CREATE-ALIAS: made '%s' in container '%s' -> Target '%s' prop '%s'",
				 alias, GetPropStr(inst, "Container"), of, prop);
		DebugPrint(dbg, __FILE__, __LINE__, CLONE);
	}
}

/* Cloning does NOT live here anymore. The bridge translates - it does not */
/* copy objects or walk a graph. The whole deep clone (the view, its       */
/* members, the aliases re-pointed at the copies, the wires re-made        */
/* between them) is one engine operation, CloneInstance (object.c), that runs  */
/* the same whoever asked. Bridge_CloneCmd below just: resolves the name,   */
/* mints the new one, calls the engine, then translates the resulting      */
/* objects into html paths and instance-created events. See                */
/* Bridge_AnnounceClone.                                                    */

/* translate one freshly-cloned instance into the session: give it its     */
/* path in the alias table, and (for an alias) fill its Target string from */
/* what its link now points at. Naming/paths are the translator's, which   */
/* is why this is here and not in the engine.                              */
static void Bridge_RegisterClone(InstanceData *local, NodeObj clone, char *pathOut, int outlen)
{
	char *cont = GetPropStr(clone, "Container");
	char *nm   = GetPropStr(clone, "Name");

	if (cont && cont[0])
		snprintf(pathOut, outlen, "%s/%s", cont, nm ? nm : "");
	else
		snprintf(pathOut, outlen, "/Root/%s", nm ? nm : "");

	RegisterPath(pathOut, clone);
}

/*
 * {"cmd":"internals","instance":"Slider_1"} - the big unification: an
 * object's control panel IS a real View populated with real Alias
 * instances, one per published data property, each a node-level link
 * into the object's own data. Built lazily server-side the first time
 * anyone asks (the instance remembers it in _Internals, so every later
 * ask - from any window, or any alias of the object - reuses the ONE
 * view), then the asking connection is told which view to open. The
 * members are ordinary instances in an ordinary view: cloneable,
 * alias-able, movable, deletable, and the view can be rearranged and
 * grown like any other. There is no second kind of control panel.
 *
 * NOTHING is held back: every published property - position, container,
 * panel coordinates, ports, all of it - is laid out, the whole of the
 * object's internal state like a frog on a dissection table. Where its
 * icon sits IS part of its state, and an alias to X moves it.
 */
void Bridge_Internals(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *of = GetPropStr(command, "instance");
	NodeObj inst, view, class, interface, prop, member, chunk;
	char viewAlias[256], memberAlias[256], base[140], num[16];
	char *existing, *name, *curAlias, *slash;
	char *escOf, *escView;
	char buf[600];
	int row = 0;

	inst = Bridge_ResolveAlias(local, of);
	if (!inst)
	{
		Bridge_Error(instance, "internals", "unknown instance");
		return;
	}

	curAlias = Bridge_AliasForInstance(local, inst);
	if (!curAlias)
		curAlias = of;

	existing = GetPropStr(inst, "_Internals");
	if (!(existing && existing[0] && ResolvePath(existing)))
	{
		/* the panel belongs to the thing it dissects - created IN it */
		view = CreateObject(inst, "View");
		if (!view)
		{
			Bridge_Error(instance, "internals", "View class not loaded");
			return;
		}

		/* named after the thing it belongs to */
		slash = strrchr(curAlias, '/');
		snprintf(base, sizeof(base), "%sPanel", slash ? slash + 1 : curAlias);
		Bridge_FreshAlias(local, curAlias, base, viewAlias, sizeof(viewAlias));

		/* a plumbing view: no icon of its own on anyone's canvas - the   */
		/* OBJECT's icon is its presence; this is the panel behind it     */
		{
			char dbg[400];
			snprintf(dbg, sizeof(dbg), "INTERNALS: panel view '%s' created in '%s'",
					 viewAlias, curAlias);
			DebugPrint(dbg, __FILE__, __LINE__, PLACE);
		}
		SetPropInt(view, "_Hidden", 1);
		SetPropStr(view, "_InternalsOf", curAlias);
		SetPropInt(view, "ReservedViewPanelX", 320);
		SetPropInt(view, "ReservedViewPanelY", 120);
		SetPropInt(view, "W", 300);

		RegisterPath(viewAlias, view);
		Bridge_SetNameFromAlias(view, viewAlias);
		/* Tell the ASKER the panel exists, and say where it really lives.
		   It is nested in the object now, so nobody stumbles across it in
		   a root listing any more - without this the client gets the panel's
		   members with a container it has never heard of and drops all of
		   them on the canvas. connId 0 was "whoever happens to be viewing
		   its container", and nobody is viewing the inside of an object. */
		Bridge_InstanceEvent(instance, local, viewAlias, "View", GetParent(view),
							 curAlias, curAlias, 1, local->replyConn);

		/* one Alias member per published property, every direction, no   */
		/* exceptions - each one a live link into the object itself. No   */
		/* events: nobody is viewing the new container yet; members        */
		/* replay when a window opens it.                                  */
		class = GetParent(inst);
		interface = class ? GetClassInterface(class) : NULL;
		for (prop = interface ? GetChild(interface) : NULL; prop; prop = GetNextSibling(prop))
		{
			name = GetPropStr(prop, "Name");
			if (!name || !name[0])
				continue;

			/* and each control is created IN the panel it appears on */
			member = CreateObject(view, "Alias");
			if (!member)
				continue;
			if (!LinkPropertyAs(member, "Value", inst, name))
			{
				DeleteInstance(member);
				continue;
			}

			SetPropStr(member, "Target", curAlias);
			SetPropStr(member, "TargetProp", name);

			/* the Interface entry is in hand - stamp the published        */
			/* presentation on the member so any client renders the row     */
			/* from the member's own properties, never from the class        */
			/* Interface (readmefirst repair #2)                              */
			SetPropInt(member, "Widget", GetPropInt(prop, "Widget"));
			SetPropStr(member, "Direction", GetPropStr(prop, "Direction"));

			SetPropStr(member, "Container", viewAlias);
			{
				char dbg[400];
				snprintf(dbg, sizeof(dbg), "INTERNALS:   member for '%s' -> container '%s'", name, viewAlias);
				DebugPrint(dbg, __FILE__, __LINE__, PLACE);
			}
			SetPropInt(member, "X", 14);
			SetPropInt(member, "Y", 12 + row * 44);
			row++;

			Bridge_FreshAlias(local, viewAlias, "Alias", memberAlias, sizeof(memberAlias));
			RegisterPath(memberAlias, member);
			Bridge_SetNameFromAlias(member, memberAlias);
		}

		snprintf(num, sizeof(num), "%d", 50 + row * 44);
		SetOrDeliverProp(view, "H", num);

		SetPropStr(inst, "_Internals", viewAlias);
		existing = GetPropStr(inst, "_Internals");
	}

	/* ALWAYS tell the asker the panel VIEW exists, not just the time it
	   gets built. It is nested inside the object now, so nobody finds it
	   in a root listing - and a client that has never been told about the
	   view cannot place a single one of its members. That is what left an
	   already-built panel completely empty for every later window. */
	{
		NodeObj pv = ResolvePath(existing);
		if (pv)
			Bridge_InstanceEvent(instance, local, existing, "View", GetParent(pv),
								 curAlias, curAlias, 1, local->replyConn);
	}

	/* tell just the asker which view is this thing's panel */
	escOf = JsonEscapeStr(curAlias);
	escView = JsonEscapeStr(existing);
	snprintf(buf, sizeof(buf), "{\"event\":\"internals\",\"instance\":%s,\"view\":%s}", escOf, escView);
	free(escOf);
	free(escView);

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, buf);
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/*
 * {"cmd":"clone-instance","of":"Slider1","container":"","x":"700","y":"250"}
 *
 * The bridge does NOT clone - it translates. It resolves what `of` names,
 * mints the new name, hands the whole job to the engine (CloneInstance,
 * object.c - which copies the view, its members, the aliases re-pointed
 * at the copies, and the wires re-made between them, identically whether
 * a script or the html asked), and then turns the resulting objects into
 * session paths and instance-created events. Cloning an alias clones the
 * THING it stands for.
 */
void Bridge_CloneCmd(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *of, *container, *x, *y, *cont, *nm, *cn, *tp;
	NodeObj src, top, map, linknode, entry, clone, cls, ln, ti;
	char path[256], panelPos[16];

	of        = GetPropStr(command, "of");
	container = GetPropStr(command, "container");
	x         = GetPropStr(command, "x");
	y         = GetPropStr(command, "y");

	src = Bridge_ResolveAlias(local, of);
	if (!src)
	{
		Bridge_Error(instance, "clone-instance", "unknown instance");
		return;
	}

	/* through an alias, clone the thing itself */
	if (strcmp(GetNameStr(GetParent(src)), "Alias") == 0)
	{
		linknode = GetPropNode(src, "Value");	/* the alias's doorway slot */
		src = linknode ? (NodeObj) GetPropLong(linknode, "LinkInst") : NULL;
		if (!src)
		{
			Bridge_Error(instance, "clone-instance", "alias has no live target");
			return;
		}
	}

	/* A thing cannot be cloned INTO itself or into something it contains -
	   that would build clones inside clones forever. Same containment-cycle
	   rule Move already refuses (ContainmentCycle, object.c). */
	{
		char srcPath[256];
		if (container && container[0] && PathOfInstance(src, srcPath, sizeof(srcPath))
			&& ContainmentCycle(srcPath, container))
		{
			Bridge_Error(instance, "clone-instance",
						 "cannot clone a view into itself");
			return;
		}
	}

	/* the engine does the whole clone AND names it - the bridge just says */
	/* what to clone and which container to put it in. map comes back as   */
	/* src -> clone for the top and every descendant.                       */
	map = NewNode(INTEGER);
	top = CloneInstance(src, container ? container : "", map);
	if (!top)
	{
		DelNode(map);
		Bridge_Error(instance, "clone-instance", "clone failed");
		return;
	}

	/* the drop point is presentation - place the top there (members keep  */
	/* their positions inside it); nudge a view's panel off the source's   */
	PlaceInstance(top, container, x, y);
	if (strcmp(GetNameStr(GetParent(top)), "View") == 0)
	{
		snprintf(panelPos, sizeof(panelPos), "%d", GetPropInt(src, "ReservedViewPanelX") + 24);
		SetOrDeliverProp(top, "ReservedViewPanelX", panelPos);
		snprintf(panelPos, sizeof(panelPos), "%d", GetPropInt(src, "ReservedViewPanelY") + 24);
		SetOrDeliverProp(top, "ReservedViewPanelY", panelPos);
	}

	/* translate the clones into the session - register every path FIRST,  */
	/* so an alias's Target (below) can resolve to a name that now exists  */
	for (entry = GetNextProp(map); entry; entry = GetNextSibling(entry))
	{
		clone = (NodeObj) GetValueLong(entry);
		if (clone)
			Bridge_RegisterClone(local, clone, path, sizeof(path));
	}

	/* then announce each, filling an alias's Target string (translation)  */
	/* from what its link now points at                                    */
	for (entry = GetNextProp(map); entry; entry = GetNextSibling(entry))
	{
		clone = (NodeObj) GetValueLong(entry);
		if (!clone)
			continue;

		cls  = GetParent(clone);
		cn   = GetNameStr(cls);
		cont = GetPropStr(clone, "Container");
		nm   = GetPropStr(clone, "Name");
		if (cont && cont[0])
			snprintf(path, sizeof(path), "%s/%s", cont, nm ? nm : "");
		else
			snprintf(path, sizeof(path), "/Root/%s", nm ? nm : "");

		if (strcmp(cn, "Alias") == 0)
		{
			ln = GetPropNode(clone, "Value");
			ti = ln ? (NodeObj) GetPropLong(ln, "LinkInst") : NULL;
			tp = ti ? Bridge_AliasForInstance(local, ti) : NULL;
			SetPropStr(clone, "Target", tp ? tp : "");
		}

		Bridge_InstanceEvent(instance, local, path, cn, cls, "Root", cont ? cont : "", 0, 0);
	}

	/* record the clone's name so the flow log carries it (Load remaps     */
	/* references onto whatever replay mints) - the engine set it, we just  */
	/* read it back                                                         */
	tp = Bridge_AliasForInstance(local, top);
	if (tp)
		SetPropStr(command, "as", tp);

	DelNode(map);
}

/*
 * {"cmd":"move-instance","of":"Slider_1","container":"/Root/MyPanel","x":"20","y":"40"}
 * - the whole drop gesture in one verb: the engine validates (a thing can
 * never be moved into itself or a descendant - MoveInstance, object.c),
 * re-containers, repositions, and the rename machinery re-keys the alias
 * and tells both containers. Same-container moves are the same verb with
 * the current container; the rename is then a no-op. What used to be a
 * client-sequenced X, Y, Container write triple (a race every raw client
 * could lose) is one fully-carried intent.
 */
static void Bridge_Rename(NodeObj instance, InstanceData *local, char *oldAlias, NodeObj inst, char *newContainer);

void Bridge_Move(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *of, *container, *x, *y, *curAlias;
	NodeObj inst;

	of        = GetPropStr(command, "of");
	container = GetPropStr(command, "container");
	x         = GetPropStr(command, "x");
	y         = GetPropStr(command, "y");

	inst = Bridge_ResolveAlias(local, of);
	if (!inst)
	{
		Bridge_Error(instance, "move-instance", "unknown instance");
		return;
	}

	curAlias = Bridge_AliasForInstance(local, inst);
	if (!curAlias)
		curAlias = of;

	/* record the CURRENT name - a stale of (the drag spanned a rename)   */
	/* resolves here, and only the current name replays                    */
	SetPropStr(command, "of", curAlias);

	if (!MoveInstance(inst, curAlias, container, x, y))
	{
		Bridge_Error(instance, "move-instance", "a thing cannot be moved into itself");
		return;
	}

	/* PlaceInstance wrote Container directly (not through set-property),  */
	/* so the alias re-key + instance-renamed events happen here            */
	Bridge_Rename(instance, local, curAlias, inst, container ? container : "");
}

/* one wire event (connected/disconnected), told to every connection      */
/* viewing either endpoint's container - the same visibility rule          */
/* instance-created follows (Bridge_SendEventScoped). The clicking client */
/* draws from THIS, never from its own gesture (readmefirst: send the     */
/* verb, act on the event), and every other window sees the same wire     */
/* appear or die. A wire spanning two different views is sent once per    */
/* view; a client viewing both dedupes by the wire's own four names.      */
static void Bridge_WireEvent(NodeObj instance, InstanceData *local, char *event,
							 NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort)
{
	char *fromAlias, *toAlias, *fromCont, *toCont;
	char *escFrom, *escFromPort, *escTo, *escToPort;
	char buf[700];

	fromAlias = Bridge_AliasForInstance(local, fromInst);
	toAlias   = Bridge_AliasForInstance(local, toInst);
	if (!fromAlias || !toAlias)
		return;		/* hidden plumbing (a raw bridge's TCP, say) - not drawable */

	escFrom     = JsonEscapeStr(fromAlias);
	escFromPort = JsonEscapeStr(fromPort ? fromPort : "");
	escTo       = JsonEscapeStr(toAlias);
	escToPort   = JsonEscapeStr(toPort ? toPort : "");

	snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"from\":%s,\"fromPort\":%s,\"to\":%s,\"toPort\":%s}",
			 event, escFrom, escFromPort, escTo, escToPort);

	free(escFrom);
	free(escFromPort);
	free(escTo);
	free(escToPort);

	fromCont = GetPropStr(fromInst, "Container");
	toCont   = GetPropStr(toInst, "Container");

	Bridge_SendEventScoped(instance, local, buf, fromCont);
	if (strcmp(fromCont ? fromCont : "", toCont ? toCont : "") != 0)
		Bridge_SendEventScoped(instance, local, buf, toCont);
}

void Bridge_Connect(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *fromAlias, *fromPort, *toAlias, *toPort;
	NodeObj fromInst, toInst;

	fromAlias = GetPropStr(command, "from");
	fromPort  = GetPropStr(command, "fromPort");
	toAlias   = GetPropStr(command, "to");
	toPort    = GetPropStr(command, "toPort");

	fromInst = Bridge_ResolveAlias(local, fromAlias);
	toInst   = Bridge_ResolveAlias(local, toAlias);

	if (!fromInst || !toInst || !fromPort || !toPort)
	{
		Bridge_Error(instance, "connect", "unknown instance or missing port");
		return;
	}

	if (!Connect(fromInst, fromPort, toInst, toPort))
	{
		Bridge_Error(instance, "connect", "connect failed");
		return;
	}

	Bridge_WireEvent(instance, local, "connected", fromInst, fromPort, toInst, toPort);
}

/*
 * {"cmd":"bind-port","container":View,"port":"In","target":Slider,
 *  "targetProp":"In"} - CONTAINER PORTS (roadmap Phase 5.1 / 2.4): make a
 * container's OWN port a transparent link to a child's port. Wiring to or
 * from the container port then resolves through to the child (ResolvePort,
 * object.c - the same link mechanism an Alias uses), so a composed View
 * can expose a curated In/Out that the outside wires to without knowing
 * what's inside. This is what makes a View a black-box widget: its In
 * forwards to an inner input control, its Out is fed by an inner output
 * control, and a script inside puppets the rest.
 */
void Bridge_BindPort(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *contAlias, *port, *targetAlias, *targetProp;
	NodeObj cont, target;

	contAlias   = GetPropStr(command, "container");
	port        = GetPropStr(command, "port");
	targetAlias = GetPropStr(command, "target");
	targetProp  = GetPropStr(command, "targetProp");

	cont   = Bridge_ResolveAlias(local, contAlias);
	target = Bridge_ResolveAlias(local, targetAlias);

	if (!cont || !target || !port || !port[0] || !targetProp || !targetProp[0])
	{
		Bridge_Error(instance, "bind-port", "container/port/target/targetProp required");
		return;
	}

	if (!LinkPropertyAs(cont, port, target, targetProp))
	{
		Bridge_Error(instance, "bind-port", "no such property on the target");
		return;
	}
}

/* {"cmd":"disconnect","from":A,"fromPort":P,"to":B,"toPort":Q} - the     */
/* inverse of connect (the mid-wire "x" in Connect mode): one engine      */
/* Disconnect(), one disconnected event, and the disconnected event is    */
/* the ONLY thing that removes a drawn wire anywhere.                     */
void Bridge_Disconnect(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *fromAlias, *fromPort, *toAlias, *toPort;
	NodeObj fromInst, toInst;

	fromAlias = GetPropStr(command, "from");
	fromPort  = GetPropStr(command, "fromPort");
	toAlias   = GetPropStr(command, "to");
	toPort    = GetPropStr(command, "toPort");

	fromInst = Bridge_ResolveAlias(local, fromAlias);
	toInst   = Bridge_ResolveAlias(local, toAlias);

	if (!fromInst || !toInst || !fromPort || !toPort)
	{
		Bridge_Error(instance, "disconnect", "unknown instance or missing port");
		return;
	}

	if (!Disconnect(fromInst, fromPort, toInst, toPort))
	{
		Bridge_Error(instance, "disconnect", "no such wire");
		return;
	}

	Bridge_WireEvent(instance, local, "disconnected", fromInst, fromPort, toInst, toPort);
}

/* Retired verbs, kept only so recorded flows replay: plain Connect()    */
/* reaches any property and any Activate now (universal default          */
/* delivery + ActivateOnMsg, object.c/node.c), so bind-property and      */
/* bind-activate are just connect spelled differently. They translate    */
/* to the same engine call and nothing else.                             */
void Bridge_BindProperty(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *fromAlias, *fromPort, *toAlias, *toProp;
	NodeObj fromInst, toInst;

	fromAlias = GetPropStr(command, "from");
	fromPort  = GetPropStr(command, "fromPort");
	toAlias   = GetPropStr(command, "to");
	toProp    = GetPropStr(command, "toProp");

	fromInst = Bridge_ResolveAlias(local, fromAlias);
	toInst   = Bridge_ResolveAlias(local, toAlias);

	if (!fromInst || !toInst || !fromPort || !toProp)
	{
		Bridge_Error(instance, "bind-property", "unknown instance or missing port/prop");
		return;
	}

	if (!Connect(fromInst, fromPort, toInst, toProp))
		Bridge_Error(instance, "bind-property", "bind failed");
}

void Bridge_BindActivate(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *fromAlias, *fromPort, *toAlias;
	NodeObj fromInst, toInst;

	fromAlias = GetPropStr(command, "from");
	fromPort  = GetPropStr(command, "fromPort");
	toAlias   = GetPropStr(command, "to");

	fromInst = Bridge_ResolveAlias(local, fromAlias);
	toInst   = Bridge_ResolveAlias(local, toAlias);

	if (!fromInst || !toInst || !fromPort)
	{
		Bridge_Error(instance, "bind-activate", "unknown instance or missing port");
		return;
	}

	if (!Connect(fromInst, fromPort, toInst, "Activate"))
		Bridge_Error(instance, "bind-activate", "bind failed");
}

/*
 * An alias is a full path (see BuildPalette/CreateInstance's own naming
 * comments) - it names where an instance currently lives, not just what
 * it's called, so moving it to a different Container really does mean a
 * different alias from here on, not a rename of a fixed identity. This is
 * the one place that happens: after the Container write lands, the old
 * path is un-registered from the ENGINE's index and the new one takes
 * over (UnregisterPath/RegisterPath - a real re-key, the trie reclaims
 * the old name), then every connected client
 * is told via instance-renamed so it can re-key its own instances/
 * propertyValues/etc maps (web/app.js's onInstanceRenamed) instead of
 * quietly going stale. Bridge_TapOnIn already resolves its alias fresh
 * per delivery rather than caching it, so live subscriptions keep
 * reporting under the correct path without needing to be told separately.
 *
 * Known gap: a previously-recorded connect instruction (local->flow, see
 * Bridge_ListConnections) still carries the old alias, so a client that
 * reconnects after a move but before ever having seen it live will fail
 * to draw that one wire on screen. Connect() itself is pointer-based, not
 * alias-based, so nothing about actual message delivery is affected -
 * this is a saved-flow display gap, not a functional one.
 */
/*
 * Renaming or moving a container re-paths its whole subtree. Every
 * descendant carries the old container path as a prefix in two places -
 * its alias key (the translator's name for it) and its Container property
 * (the engine's fact of where it lives) - and BOTH must swap old -> new,
 * or the members are orphaned: a clone finds zero of them (CloneInstance walks
 * by Container), and a set-property by the new path misses. Every
 * connection viewing any part of the subtree has its view-keys migrated
 * too and gets an instance-renamed per descendant so it re-keys its own
 * rendering. The alias table and each conn's view table are snapshotted
 * before rewriting, because the rewrite mutates the very chains walked.
 */
static void Bridge_RepathSubtree(NodeObj instance, InstanceData *local, char *oldAlias, char *newAlias)
{
	NodeObj snap, entry, inst, ce, table, ks, ke, lib, cls;
	char prefix[300], newP[512], newCont[512], oldCont[300], buf[1200], key[24], nk[560], dbg[1400], pbuf[300];
	char *p, *cont, *k, *ok, *escFrom, *escTo, *slash;
	long cut;
	int preLen, oldLen, n;

	oldLen = (int) strlen(oldAlias);
	snprintf(prefix, sizeof(prefix), "%s/", oldAlias);
	preLen = (int) strlen(prefix);

	snprintf(dbg, sizeof(dbg), "REPATH subtree: '%s' -> '%s'", oldAlias, newAlias);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	/* --- the instances: snapshot descendants, then re-path each.        */
	/* Enumeration comes from the REGISTRY (the engine's path index is a   */
	/* trie - lookups, not walks); each instance's current path derives     */
	/* from its own Name/Container, which still carry the OLD prefix here.  */
	snap = NewNode(INTEGER);
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
	  for (inst = GetChild(cls); inst; inst = GetNextSibling(inst))
	  {
		if (!PathOfInstance(inst, pbuf, sizeof(pbuf)))
			continue;
		if (strncmp(pbuf, prefix, preLen) != 0)
			continue;	/* not a descendant */
		SetPropLong(snap, pbuf, (long) inst);	/* key = old path, value = instance */
	  }

	for (entry = GetNextProp(snap); entry; entry = GetNextSibling(entry))
	{
		p = GetNameStr(entry);			/* old path */
		inst = (NodeObj) GetValueLong(entry);

		snprintf(newP, sizeof(newP), "%s/%s", newAlias, p + preLen);

		/* re-key the path in the engine's index */
		UnregisterPath(p);
		RegisterPath(newP, inst);

		/* the old container is where a viewing conn is registered - read  */
		/* it off the old path before we change Container                   */
		slash = strrchr(p, '/');
		cut = slash ? (long)(slash - p) : 0;
		if (cut > 0 && cut < (long) sizeof(oldCont))
		{
			memcpy(oldCont, p, cut);
			oldCont[cut] = 0;
		}
		else
			oldCont[0] = 0;
		if (strcmp(oldCont, "/Root") == 0)
			oldCont[0] = 0;

		/* swap the old container prefix in its Container property */
		cont = GetPropStr(inst, "Container");
		if (cont && strncmp(cont, oldAlias, oldLen) == 0)
		{
			if (cont[oldLen] == 0)
				snprintf(newCont, sizeof(newCont), "%s", newAlias);
			else
				snprintf(newCont, sizeof(newCont), "%s%s", newAlias, cont + oldLen);
			SetOrDeliverProp(inst, "Container", newCont);
		}

		snprintf(dbg, sizeof(dbg), "REPATH:   member '%s' (%s) -> '%s'  (Container now '%s')",
				 p, GetNameStr(GetParent(inst)), newP, GetPropStr(inst, "Container"));
		DebugPrint(dbg, __FILE__, __LINE__, CLONE);

		/* tell whoever is viewing where it lived */
		escFrom = JsonEscapeStr(p);
		escTo   = JsonEscapeStr(newP);
		snprintf(buf, sizeof(buf), "{\"event\":\"instance-renamed\",\"from\":%s,\"to\":%s}", escFrom, escTo);
		free(escFrom);
		free(escTo);
		Bridge_SendEventScoped(instance, local, buf, oldCont);
	}
	DelNode(snap);

	/* --- aliases: rewrite any Target STRING naming the moved subtree --- */
	/* An alias carries a Target string (watchable metadata the GUI binds   */
	/* to; the link itself is by pointer and needs no fixup). A member       */
	/* alias pointing at a sibling in the subtree, or an outside alias       */
	/* pointing in, now names a path that no longer exists - its control     */
	/* can't bind, so it looks dead even though the engine link is fine.     */
	/* This is what left the renamed view's own alias broken while a clone's  */
	/* (announced with a fresh Target) worked. Swap the prefix, delivered so  */
	/* the GUI re-binds.                                                      */
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
	  for (inst = GetChild(cls); inst; inst = GetNextSibling(inst))
	  {
		if (!CmpName(cls, "Alias"))
			continue;
		if (!PathOfInstance(inst, pbuf, sizeof(pbuf)))
			continue;
		p = pbuf;

		cont = GetPropStr(inst, "Target");	/* reuse cont as the target string */
		if (cont && strncmp(cont, oldAlias, oldLen) == 0 && (cont[oldLen] == 0 || cont[oldLen] == '/'))
		{
			if (cont[oldLen] == 0)
				snprintf(newCont, sizeof(newCont), "%s", newAlias);
			else
				snprintf(newCont, sizeof(newCont), "%s%s", newAlias, cont + oldLen);

			snprintf(dbg, sizeof(dbg), "REPATH:   alias '%s' Target '%s' -> '%s'", p, cont, newCont);
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);

			SetOrDeliverProp(inst, "Target", newCont);
		}
	}

	/* --- the viewers: migrate every conn's view-keys old -> new --- */
	for (ce = GetNextProp(local->connViews); ce; ce = GetNextSibling(ce))
	{
		table = (NodeObj) GetValueLong(ce);
		if (!table)
			continue;

		/* snapshot matching keys, then add the new ones (mutating table) */
		ks = NewNode(INTEGER);
		n = 0;
		for (ke = GetNextProp(table); ke; ke = GetNextSibling(ke))
		{
			k = GetNameStr(ke);
			if (!k)
				continue;
			/* a view-key is "<container>|" - the subtree is oldAlias| or  */
			/* anything under oldAlias/                                     */
			if (strncmp(k, oldAlias, oldLen) == 0 && (k[oldLen] == '|' || k[oldLen] == '/'))
			{
				snprintf(key, sizeof(key), "%d", n++);
				SetPropStr(ks, key, k);
			}
		}
		for (ke = GetNextProp(ks); ke; ke = GetNextSibling(ke))
		{
			ok = GetValueStr(ke);
			snprintf(nk, sizeof(nk), "%s%s", newAlias, ok + oldLen);
			SetPropInt(table, nk, 1);
		}
		DelNode(ks);
	}
}

static void Bridge_Rename(NodeObj instance, InstanceData *local, char *oldAlias, NodeObj inst, char *newContainer)
{
	char newAlias[256];
	char *slash, *baseName;
	char buf[512];
	char *escFrom, *escTo;

	slash = strrchr(oldAlias, '/');
	baseName = slash ? slash + 1 : oldAlias;

	if (newContainer && newContainer[0])
		snprintf(newAlias, sizeof(newAlias), "%s/%s", newContainer, baseName);
	else
		snprintf(newAlias, sizeof(newAlias), "/Root/%s", baseName);

	if (strcmp(newAlias, oldAlias) == 0)
		return;

	UnregisterPath(oldAlias);
	RegisterPath(newAlias, inst);

	escFrom = JsonEscapeStr(oldAlias);
	escTo   = JsonEscapeStr(newAlias);
	snprintf(buf, sizeof(buf), "{\"event\":\"instance-renamed\",\"from\":%s,\"to\":%s}", escFrom, escTo);
	free(escFrom);
	free(escTo);

	/* a move is visible from both ends: whoever is looking at where it   */
	/* was, and whoever is looking at where it went                        */
	{
		char oldCont[256];
		long cut = slash ? (long)(slash - oldAlias) : 0;

		if (cut > 0 && cut < (long) sizeof(oldCont))
		{
			memcpy(oldCont, oldAlias, cut);
			oldCont[cut] = 0;
		}
		else
			oldCont[0] = 0;
		if (strcmp(oldCont, "/Root") == 0)
			oldCont[0] = 0;

		Bridge_SendEventScoped(instance, local, buf, oldCont);
		if (strcmp(oldCont[0] ? oldCont : "/", (newContainer && newContainer[0]) ? newContainer : "/") != 0)
			Bridge_SendEventScoped(instance, local, buf, newContainer);
	}

	/* the thing moved - everything inside it moves with it */
	Bridge_RepathSubtree(instance, local, oldAlias, newAlias);
}

/* the Name property's write-side: same alias re-keying as a Container    */
/* move, but the basename changes and the container stays - what a thing  */
/* is called is just one of its properties. Collisions and slashes are    */
/* rejected, and the Name property is put back to match reality either    */
/* way.                                                                    */
static void Bridge_RenameName(NodeObj instance, InstanceData *local, char *oldAlias, NodeObj inst, char *newName)
{
	char newAlias[512], oldCont[256];
	char *slash;
	long cut;
	char buf[900];
	char *escFrom, *escTo;

	slash = strrchr(oldAlias, '/');
	cut = slash ? (long)(slash - oldAlias) : 0;
	if (cut > 0 && cut < (long) sizeof(oldCont))
	{
		memcpy(oldCont, oldAlias, cut);
		oldCont[cut] = 0;
	}
	else
		oldCont[0] = 0;

	if (!newName || !newName[0] || strchr(newName, '/'))
	{
		Bridge_SetNameFromAlias(inst, oldAlias);	/* put it back */
		Bridge_Error(instance, "set-property", "Name must be non-empty, without '/'");
		return;
	}

	snprintf(newAlias, sizeof(newAlias), "%s/%s", oldCont[0] ? oldCont : "/Root", newName);

	if (strcmp(newAlias, oldAlias) == 0)
		return;

	if (ResolvePath(newAlias))
	{
		Bridge_SetNameFromAlias(inst, oldAlias);	/* put it back */
		Bridge_Error(instance, "set-property", "that name is already taken here");
		return;
	}

	UnregisterPath(oldAlias);
	RegisterPath(newAlias, inst);

	escFrom = JsonEscapeStr(oldAlias);
	escTo   = JsonEscapeStr(newAlias);
	snprintf(buf, sizeof(buf), "{\"event\":\"instance-renamed\",\"from\":%s,\"to\":%s}", escFrom, escTo);
	free(escFrom);
	free(escTo);

	/* a pure rename is only visible where the thing lives */
	if (strcmp(oldCont, "/Root") == 0)
		oldCont[0] = 0;
	Bridge_SendEventScoped(instance, local, buf, oldCont);

	{
		char dbg[1024];
		snprintf(dbg, sizeof(dbg), "RENAME: '%s' -> '%s'  (position unchanged: X=%s Y=%s PanelX=%s PanelY=%s)",
				 oldAlias, newAlias, GetPropStr(inst, "X"), GetPropStr(inst, "Y"),
				 GetPropStr(inst, "ReservedViewPanelX"), GetPropStr(inst, "ReservedViewPanelY"));
		DebugPrint(dbg, __FILE__, __LINE__, CLONE);
	}

	/* the thing was renamed - everything inside it is re-pathed with it */
	Bridge_RepathSubtree(instance, local, oldAlias, newAlias);
}

void Bridge_Set(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *alias, *prop, *value;
	NodeObj inst;

	alias = GetPropStr(command, "instance");
	prop  = GetPropStr(command, "prop");
	value = GetPropStr(command, "value");

	inst = Bridge_ResolveAlias(local, alias);

	if (!inst || !prop || !value)
	{
		Bridge_Error(instance, "set-property", "unknown instance or missing prop/value");
		return;
	}

	/* Name and Container are ordinary properties whose value happens to  */
	/* BE the thing's identity/location - writing either re-keys the       */
	/* alias. Judged on the RESOLVED property (a dissection-table member's */
	/* "Value" slot may stand for the target's Name), and applied to the    */
	/* resolved owner: setting them through a member renames/moves the      */
	/* THING, never the member. The owner's CURRENT path is captured        */
	/* BEFORE the write lands: the reverse lookup derives from Name +       */
	/* Container (PathOfInstance), so writing Name first would make the      */
	/* thing unaddressable mid-rename and the re-key would silently skip.    */
	{
		NodeObj owner = inst;
		NodeObj node = ResolvePort(&owner, prop);
		char *realProp = node ? GetNameStr(node) : prop;
		char *ownAlias = Bridge_AliasForInstance(local, owner);

		SetOrDeliverProp(inst, prop, value);

		/* position/geometry only - trace it (the Value stream would drown  */
		/* the log); shows the panel-position writes landing on load        */
		if (strcmp(prop, "X") == 0 || strcmp(prop, "Y") == 0
			|| strcmp(prop, "W") == 0 || strcmp(prop, "H") == 0
			|| strcmp(prop, "ReservedViewPanelX") == 0 || strcmp(prop, "ReservedViewPanelY") == 0)
		{
			char dbg[300];
			snprintf(dbg, sizeof(dbg), "SET-POS: %s.%s = %s", alias, prop, value);
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
		}

		/* the flow log records the FACT, not the doorway it came through: */
		/* a write through an alias (a panel member, a dragged-out control) */
		/* is a write on the original, and only the original is replayable  */
		/* - internals views are rebuilt lazily, never recorded, so a       */
		/* command naming a member alias would land on nothing at Load      */
		if (node && ownAlias)
		{
			SetPropStr(command, "instance", ownAlias);
			SetPropStr(command, "prop", realProp);
		}

		if (strcmp(realProp, "Container") == 0 || strcmp(realProp, "Name") == 0)
		{
			if (ownAlias)
			{
				if (strcmp(realProp, "Container") == 0)
					Bridge_Rename(instance, local, ownAlias, owner, value);
				else
					Bridge_RenameName(instance, local, ownAlias, owner, value);
			}
			else
				/* an identity write on something with no resolvable path  */
				/* is a re-key that cannot happen - never silent            */
				DebugPrint("SET: Name/Container write on an instance with no resolvable path - no re-key", __FILE__, __LINE__, ERROR);
		}
	}
}

void Bridge_DoActivate(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *alias;
	NodeObj inst;

	alias = GetPropStr(command, "instance");
	inst  = Bridge_ResolveAlias(local, alias);

	if (!inst)
	{
		Bridge_Error(instance, "activate", "unknown instance");
		return;
	}

	ActivateInstance(inst);
}

/*
 * {"cmd":"delete-instance","instance":"Reader1"} - deliberately uses
 * ResolvePath directly rather than Bridge_ResolveAlias, since Chrome
 * (the topbar's File/Mode menus - real app UI, not a session object) is
 * never in the path index and so never a delete-instance target. The
 * Palette's own bootstrap instances ARE resolvable (they're ordinary
 * Root instances), so what stops them from being deleted is the same
 * Deletable="0" property BuildPalette (object.c) already set on them -
 * an ordinary property check, not a structural exclusion.
 *
 * The path is UnregisterPath'd - a real delete; the trie reclaims the
 * key (the old zeroed-alias non-cleanup went with the per-bridge
 * table).
 *
 * Stale-subscriber gap, closed July 2026: DeleteInstance itself now
 * scrubs every Subscriber registry-wide that points at the dying
 * instance (ScrubRegistrySubscriptions, object.c) and cancels its
 * queued sends, so wiring TO a deleted instance no longer dangles.
 * The one thing DeleteInstance can't know about is taps - they're
 * bare bookkeeping nodes, not registered instances - so they're freed
 * here first (Bridge_FreeTaps).
 */

/* free every tap subscribed to any of inst's ports, ahead of deleting  */
/* inst - the Subscriber props pointing at them die with the instance,  */
/* and a tap is a bare node nothing else owns (Bridge_MakeTap mallocs   */
/* it, no registry, no parent), so each one would otherwise leak        */
void Bridge_FreeTaps(NodeObj inst)
{
	NodeObj port, sub, tap;

	for (port = GetNextProp(inst); port; port = GetNextSibling(port))
	{
		for (sub = GetNextProp(port); sub; sub = GetNextSibling(sub))
		{
			if (!CmpName(sub, "Subscriber"))
				continue;
			tap = (NodeObj) GetPropLong(sub, "Instance");
			if (tap && CmpName(tap, "Tap"))
				DelNode(tap);
		}
	}
}

/* defined below - Bridge_Delete reclaims the dead alias's recorded history */
void Bridge_CompactFlow(InstanceData *local, char *alias);

void Bridge_Delete(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *alias, *escAlias, *deletable;
	NodeObj inst;
	char buf[256];

	alias = GetPropStr(command, "instance");
	inst  = alias ? ResolvePath(alias) : NULL;

	if (!inst)
	{
		Bridge_Error(instance, "delete-instance", "unknown instance");
		return;
	}

	deletable = GetPropStr(inst, "Deletable");
	if (deletable && strcmp(deletable, "0") == 0)
	{
		Bridge_Error(instance, "delete-instance", "this instance is not deletable");
		return;
	}

	{
		/* Deleting a CONTAINER deletes everything it contains. Containment
		   is a Container-property relationship, not tree parentage, so a
		   view's members are NOT children of the view node - DeleteInstance
		   on the view alone would leave every slider, alias and nested view
		   still registered in the path index and the registry, orphaned.
		   Then the next clone into the same name collides with those stale
		   entries and comes up empty (the reported bug). So collect the
		   whole subtree first - the container and every descendant, by path
		   prefix, exactly the way a rename re-paths it - and delete each.
		   Snapshot before deleting, because deleting mutates the registry
		   this walks. */
		NodeObj snap, entry, lib, cls, mem;
		char prefix[300], pbuf[300], cont[300];
		int preLen;
		char *p, *slash;

		snprintf(prefix, sizeof(prefix), "%s/", alias);
		preLen = (int) strlen(prefix);

		snap = NewNode(INTEGER);
		/* the container itself, keyed by its own path */
		SetPropLong(snap, alias, (long) inst);
		/* then every descendant */
		for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
		  for (mem = GetChild(cls); mem; mem = GetNextSibling(mem))
		  {
			if (!PathOfInstance(mem, pbuf, sizeof(pbuf)))
				continue;
			if (strncmp(pbuf, prefix, preLen) != 0)
				continue;	/* not inside this container */
			SetPropLong(snap, pbuf, (long) mem);
		  }

		for (entry = GetNextProp(snap); entry; entry = GetNextSibling(entry))
		{
			p = GetNameStr(entry);			/* this instance's path */
			mem = (NodeObj) GetValueLong(entry);
			if (!mem)
				continue;

			/* where it lived - the parent path - for the scoped event */
			slash = strrchr(p, '/');
			if (slash && slash != p)
			{
				int cut = (int)(slash - p);
				if (cut >= (int) sizeof(cont))
					cut = (int) sizeof(cont) - 1;
				memcpy(cont, p, cut);
				cont[cut] = 0;
			}
			else
				snprintf(cont, sizeof(cont), "/Root");
			/* scope the removal to the EXACT container the client views -
			   create scopes to the raw Container (e.g. "/Root"), and the
			   view key for "/Root" is NOT the same as for "" (Bridge_ViewKey
			   maps "" -> "/"). Collapsing /Root to "" here sent the top
			   view's removal to a key nobody was viewing, so its icon and
			   panel lingered until a reload. */

			Bridge_FreeTaps(mem);
			UnregisterPath(p);
			DeleteInstance(mem);
			Bridge_CompactFlow(local, p);

			escAlias = JsonEscapeStr(p);
			snprintf(buf, sizeof(buf), "{\"event\":\"instance-removed\",\"instance\":%s}", escAlias);
			free(escAlias);
			Bridge_SendEventScoped(instance, local, buf, cont);
		}

		DelNode(snap);
	}
}

/*
 * A connection died (msg_eof arrived on In, tagged with its Conn id -
 * forwarded up from TCP through WebSocket, or straight from TCP on the
 * raw-JSON bridges; Conn 0 means the transport itself shut down, every
 * peer at once): free the closing connection's view table.
 *
 * There used to be a per-connection ownership sweep here (_OwnerConn):
 * the hidden helper widgets the OLD client created for every card were
 * one connection's private chrome, and had to die with it or every page
 * reload leaked a full set. That whole species is gone - a card's rows
 * are the engine's own internals-view members now (readmefirst repair
 * #3), shared session state like everything else - so no instance is
 * per-connection anymore and there is nothing to sweep. Everything a
 * client makes is session content that survives its creator, which is
 * the point of the shared session.
 */

/* drop every command in the flow log that references alias in any of   */
/* the fields the wire protocol uses to name an instance                */
void Bridge_CompactFlow(InstanceData *local, char *alias)
{
	NodeObj entry, next;
	char *fields[] = { "as", "instance", "from", "to", "of" };
	int i, hit;
	char *v;

	if (!local->flow)
		return;

	entry = GetChild(local->flow);
	while (entry)
	{
		next = GetNextSibling(entry);

		hit = 0;
		for (i = 0; i < 5 && !hit; i++)
		{
			v = GetPropStr(entry, fields[i]);
			if (v && strcmp(v, alias) == 0)
				hit = 1;
		}

		if (hit)
		{
			DelSibling(entry);
			DelNode(entry);
		}

		entry = next;
	}
}

void Bridge_ConnClosed(NodeObj instance, InstanceData *local, long connId)
{
	NodeObj entry;

	/* the closing connection stops looking at everything - free its   */
	/* view table (Conn 0 = the transport shut down: free them all)    */
	if (connId == 0)
	{
		for (entry = GetNextProp(local->connViews); entry; entry = GetNextSibling(entry))
		{
			NodeObj table = (NodeObj) GetValueLong(entry);
			if (table)
			{
				DelNode(table);
				/* SetValueLong(entry, 0) would silently no-op (node.c    */
				/* guards zero) - go through SetConnState instead          */
				SetConnState(local->connViews, atol(GetNameStr(entry)), 0);
			}
		}
	}
	else
	{
		NodeObj table = (NodeObj) GetConnState(local->connViews, connId);
		if (table)
		{
			DelNode(table);
			SetConnState(local->connViews, connId, 0);
		}
	}
}

/*
 * Live taps: "subscribe" attaches a JSON-emitting probe variant to any
 * port or property, so a client watching one gets property-changed or
 * message-flowed events without polling. A tap is not a registered
 * class instance the way Reader/Pulse/etc are - it is not something a
 * user drags onto a palette, it is the Bridge's own plumbing - so it is
 * just a bare node carrying an OnMsg handler, exactly the shape
 * Connect()/AddSubscription need on their "to" side. Rather than fan
 * out through its own Out port (which would need its own wiring to
 * whatever transport the Bridge itself sits behind), the tap holds a
 * direct reference to the Bridge instance and calls SndMsg on the
 * Bridge's own Out - reusing the wiring the Bridge already has to the
 * client, whether that is raw TCP or a WebSocket, for free.
 */
int Bridge_TapOnIn(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj owner, target, chunk;
	InstanceData *ownerLocal;
	char *alias, *port, *eventType, *value;
	char *escAlias, *escPort, *escValue;
	char *buf;
	int   bufLen;

	owner = (NodeObj) GetPropLong(instance, "Owner");
	if (!owner)
		return rtrn_propagate;

	/* resolved fresh every delivery, not cached at subscribe time - see  */
	/* Bridge_AliasForInstance's doc comment. Falls back to the string    */
	/* stashed at subscribe time for Chrome taps (MenuButtons aren't in   */
	/* local->aliases and never rename anyway).                          */
	target     = (NodeObj) GetPropLong(instance, "Target");
	ownerLocal = (InstanceData *) GetPropLong(owner, "local");
	alias      = (target && ownerLocal) ? Bridge_AliasForInstance(ownerLocal, target) : NULL;
	if (!alias)
		alias = GetPropStr(instance, "Instance");

	port      = GetPropStr(instance, "Port");
	eventType = GetPropStr(instance, "EventType");
	value     = data ? GetValueStr(data) : "";

	escAlias = JsonEscapeStr(alias ? alias : "");
	escPort  = JsonEscapeStr(port ? port : "");
	escValue = JsonEscapeStr(value ? value : "");

	/* size the buffer to the actual value - a fixed buffer silently dropped
	   any large property (a README's worth of help Markdown is ~2.5KB, well
	   past a small fixed size). Same malloc pattern as instance-created. */
	bufLen = (int)(strlen(escAlias) + strlen(escPort) + strlen(escValue)
			 + strlen(eventType ? eventType : "message-flowed") + 64);
	buf = malloc(bufLen);

	snprintf(buf, bufLen, "{\"event\":\"%s\",\"instance\":%s,\"port\":%s,\"value\":%s}",
			 eventType ? eventType : "message-flowed", escAlias, escPort, escValue);

	free(escAlias);
	free(escPort);
	free(escValue);

	/* one targeted copy per connection that subscribed through this tap  */
	/* - never a blind broadcast; a window that never asked about this     */
	/* thing never hears about it (the "Conns" table is filled by          */
	/* Bridge_Subscribe, one entry per subscribing connection)             */
	{
		NodeObj conns = GetPropNode(instance, "Conns");
		NodeObj entry;
		int sent = 0;

		for (entry = conns ? GetNextProp(conns) : NULL; entry; entry = GetNextSibling(entry))
		{
			/* SetConnState stores LONGs - GetValueInt on a LONG reads 0 */
			if (!GetValueLong(entry))
				continue;
			chunk = NewNode(STRING);
			SetName(chunk, "Event");
			SetValueStr(chunk, buf);
			SetPropLong(chunk, "Conn", atol(GetNameStr(entry)));
			SndMsg(owner, "Out", msg_send, chunk);
			sent = 1;
		}

		/* a tap with no recorded conns (made internally, not through a  */
		/* client subscribe) keeps the old broadcast behavior             */
		if (!sent)
		{
			chunk = NewNode(STRING);
			SetName(chunk, "Event");
			SetValueStr(chunk, buf);
			SndMsg(owner, "Out", msg_send, chunk);
		}
	}

	free(buf);
	return rtrn_propagate;
}

NodeObj Bridge_MakeTap(NodeObj bridgeInstance, NodeObj target, char *alias, char *port, char *eventType)
{
	NodeObj tap, in;

	tap = NewNode(INTEGER);
	SetName(tap, "Tap");
	SetPropStr(tap, "Instance", alias);	/* fallback only - see Bridge_TapOnIn */
	SetPropLong(tap, "Target", (long) target);
	SetPropStr(tap, "Port", port);
	SetPropStr(tap, "EventType", eventType);
	SetPropLong(tap, "Owner", (long) bridgeInstance);

	SetPropInt(tap, "In", 0);
	in = GetPropNode(tap, "In");
	SetPropLong(in, "OnMsg", (long) Bridge_TapOnIn);

	return tap;
}

void Bridge_Subscribe(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *alias, *port, *eventType, *name;
	NodeObj inst, class, interface, prop, tap;

	alias = GetPropStr(command, "instance");
	port  = GetPropStr(command, "port");

	inst = Bridge_ResolveAlias(local, alias);

	if (!inst || !port || !port[0])
	{
		char em[300];
		snprintf(em, sizeof(em), "unknown instance '%s' or missing port '%s'",
				 alias ? alias : "", port ? port : "");
		Bridge_Error(instance, "subscribe", em);
		return;
	}

	/* subscribing to an alias IS subscribing to the original: resolve   */
	/* and relabel up front, so the eventType lookup, the tap, its       */
	/* dedupe, and every event it emits all speak the original's name -  */
	/* one tap serves the original and every alias of it (clients render */
	/* aliases from Target/TargetProp and subscribe to the target, so    */
	/* the names line up)                                                 */
	{
		NodeObj owner = inst;
		char *realAlias;

		if (ResolvePort(&owner, port) && owner != inst)
		{
			inst = owner;
			realAlias = Bridge_AliasForInstance(local, inst);
			if (realAlias)
				alias = realAlias;
		}
	}

	/* the source class already published what this property is (Phase */
	/* 1.4) - a "data" property is watched as property-changed, an     */
	/* "in"/"out" port as message-flowed, so the client never has to    */
	/* tell the bridge which kind it is asking for                      */
	eventType = "message-flowed";
	class = GetParent(inst);
	interface = class ? GetClassInterface(class) : NULL;
	prop = interface ? GetChild(interface) : NULL;
	while (prop)
	{
		char *direction;

		name = GetPropStr(prop, "Name");
		if (name && strcmp(name, port) == 0)
		{
			direction = GetPropStr(prop, "Direction");
			if (direction && strcmp(direction, "data") == 0)
				eventType = "property-changed";
			break;
		}
		prop = GetNextSibling(prop);
	}

	/* reconnects re-subscribe to everything they can see, so without    */
	/* this check every page load stacked another identical tap on the   */
	/* same port - N taps after N reloads, N copies of every event to    */
	/* every client, N-1 of them leaked. Taps broadcast (Conn 0), so     */
	/* one tap per port+event already serves every client at once.       */
	tap = NULL;
	{
		NodeObj portNode, sub, cand;
		char *tPort, *tEvent;

		portNode = GetPropNode(inst, port);
		for (sub = portNode ? GetNextProp(portNode) : NULL; sub; sub = GetNextSibling(sub))
		{
			if (!CmpName(sub, "Subscriber"))
				continue;
			cand = (NodeObj) GetPropLong(sub, "Instance");
			if (!cand || !CmpName(cand, "Tap"))
				continue;
			if ((NodeObj) GetPropLong(cand, "Owner") != instance)
				continue;
			tPort  = GetPropStr(cand, "Port");
			tEvent = GetPropStr(cand, "EventType");
			if (tPort && strcmp(tPort, port) == 0 && tEvent && strcmp(tEvent, eventType) == 0)
			{
				tap = cand;
				break;
			}
		}
	}

	if (!tap)
	{
		tap = Bridge_MakeTap(instance, inst, alias, port, eventType);

		if (!Connect(inst, port, tap, "In"))
		{
			DelNode(tap);
			Bridge_Error(instance, "subscribe", "connect failed");
			return;
		}
	}

	/* the tap remembers each connection that subscribed - its events go  */
	/* to exactly those connections, never broadcast (Bridge_TapOnIn)      */
	{
		NodeObj conns = GetPropNode(tap, "Conns");
		if (!conns)
		{
			SetPropInt(tap, "Conns", 0);
			conns = GetPropNode(tap, "Conns");
		}
		SetConnState(conns, local->replyConn, 1);
	}

	/* subscribing is truth-on-demand, not just a promise of future deltas -  */
	/* a data property already has a current value sitting right there, so   */
	/* hand it over immediately instead of leaving the client showing        */
	/* nothing (or a stale default) until the next unrelated write happens   */
	/* to touch it. A port has no such resting value, so this only applies   */
	/* to "data" properties.                                                  */
	if (strcmp(eventType, "property-changed") == 0)
		Bridge_TapOnIn(tap, msg_change, GetPropNode(inst, port));
}

/* targeted - only the client that logged in needs to see it */
void Bridge_LoggedIn(NodeObj instance, InstanceData *local, char *user)
{
	char buf[256];
	char *escUser;
	NodeObj chunk;

	escUser = JsonEscapeStr(user ? user : "");
	snprintf(buf, sizeof(buf), "{\"event\":\"logged-in\",\"user\":%s}", escUser);
	free(escUser);

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, buf);
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/* token auth: {"cmd":"login","user":"<Main/Users name>","token":"..."} */
/* checked against AuthenticateUser (object.c) - Main must be set on    */
/* this instance (see InstanceStart) or there is no Users tree to check.*/
/* Authenticated state is per Conn - one client logging in must not     */
/* authenticate every other viewer sharing this same Bridge instance.   */
void Bridge_Login(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *name, *token;
	NodeObj main, user;

	name  = GetPropStr(command, "user");
	token = GetPropStr(command, "token");

	main = (NodeObj) GetPropLong(instance, "Main");
	user = main ? AuthenticateUser(main, name, token) : NULL;

	if (!user)
	{
		Bridge_Error(instance, "login", "invalid user or token");
		return;
	}

	SetConnState(local->connAuth, local->replyConn, 1);
	Bridge_LoggedIn(instance, local, name);
}

/*
 * The single thing a connecting client gets told about: the view. There
 * is no separate "what classes exist" protocol, and no separate palette
 * category either - the palette (see GetPalette/BuildPalette, object.c)
 * is a real View plus one real, inert instance per registered class,
 * seeded into local->aliases at InstanceStart below the moment this
 * Bridge came alive, so the Root walk here already carries them; the
 * client tells a palette entry apart from anything else purely by its
 * own Container/Deletable properties, not by which group list-instances
 * put it in. Targeted to just the connection that asked - the rest of
 * the shared view already saw the session's instances arrive live and
 * doesn't need them replayed.
 */
/*
 * Scoped to what's visible: an optional "container" on the command asks
 * for one container's direct members only; absent (or "") means the top
 * level - the root canvas plus the app chrome. A client starts with the
 * root and asks for a view's members the first time that view opens -
 * the GUI never hears about the inside of a panel nobody has opened.
 * There could eventually be millions of objects in a session; a window
 * only ever needs the ones it can see.
 */
void Bridge_ListInstances(NodeObj instance, InstanceData *local, NodeObj command)
{
	NodeObj inst, class, chunk;
	char *container, *cont;

	container = command ? GetPropStr(command, "container") : NULL;
	if (!container || !container[0])
		container = "/Root";	/* the canvas IS the root view */

	/* asking to list a container IS looking at it - from here on this   */
	/* connection receives live events about it, and only about places    */
	/* it has looked                                                       */
	Bridge_MarkViewing(local, local->replyConn, container);

	/* NO CATEGORIES. There is no chrome, no palette, no root, no special
	   anything - just what is in the view being asked about. The menus and
	   the palette are instances in the root view, so the ordinary walk
	   below finds them exactly like it finds a slider a user dragged out. */

	{
		NodeObj lib;
		char pbuf[300];

		for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		 for (class = GetChild(lib); class; class = GetNextSibling(class))
		  for (inst = GetChild(class); inst; inst = GetNextSibling(inst))
		  {
			/* unnamed engine internals derive no path - they were never  */
			/* in the alias table before either                            */
			if (!PathOfInstance(inst, pbuf, sizeof(pbuf)))
				continue;
			cont = GetPropStr(inst, "Container");
			if (strcmp(cont ? cont : "", container) == 0)
				Bridge_InstanceEvent(instance, local, pbuf, GetNameStr(class), class, "Root", cont, GetPropInt(inst, "_Hidden"), local->replyConn);
		  }
	}

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, "{\"event\":\"instances-done\"}");
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/*
 * A client entering Connect mode (app.js) asks "draw all connections."
 * The truth about what is wired is the live subscription graph itself -
 * the Subscriber entries a Connect() lands on a source port (object.c) -
 * NOT the flow log. This used to walk the flow's recorded `connect`
 * commands, which misses every wire the log never saw: the ones a view
 * CLONE makes between its members (CloneConnections), which is why a
 * cloned flow came up on screen with no lines even though it worked.
 * Walking the graph reports every real wire once, whatever made it, and
 * cannot double a loaded/cloned one - it is simply what exists now.
 *
 * Every record self-describes ({Instance, Port} - AddSubscription,
 * object.c), so a property-to-property wire reports the same as a wire
 * into a compiled port. Bridge taps are filtered out by having no alias
 * (a tap is the Bridge's own plumbing, not a drawable wire).
 */
void Bridge_ListConnections(NodeObj instance, InstanceData *local)
{
	NodeObj port, sub, sink, chunk, lib, cls;
	NodeObj inst;
	char *fromAlias, *toAlias, *toPort;
	char fromBuf[300];
	char *escFrom, *escFromPort, *escTo, *escToPort;
	char buf[600];

	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
	  for (inst = GetChild(cls); inst; inst = GetNextSibling(inst))
	  {
		/* unnamed - not addressable, so not a drawable wire end */
		if (!PathOfInstance(inst, fromBuf, sizeof(fromBuf)))
			continue;
		fromAlias = fromBuf;

		/* every property is a potential source port; the wired ones are  */
		/* exactly those carrying Subscriber entries                       */
		for (port = GetNextProp(inst); port; port = GetNextSibling(port))
		{
			for (sub = GetNextProp(port); sub; sub = GetNextSibling(sub))
			{
				if (!CmpName(sub, "Subscriber"))
					continue;

				sink = (NodeObj) GetPropLong(sub, "Instance");
				toAlias = sink ? Bridge_AliasForInstance(local, sink) : NULL;
				if (!toAlias)
					continue;	/* a tap - not a drawable wire */

				toPort = GetPropStr(sub, "Port");
				if (!toPort)
					continue;	/* a record predating port-carrying subscriptions */

				escFrom     = JsonEscapeStr(fromAlias);
				escFromPort = JsonEscapeStr(GetNameStr(port));
				escTo       = JsonEscapeStr(toAlias);
				escToPort   = JsonEscapeStr(toPort);

				snprintf(buf, sizeof(buf), "{\"event\":\"connected\",\"from\":%s,\"fromPort\":%s,\"to\":%s,\"toPort\":%s}",
						 escFrom, escFromPort, escTo, escToPort);

				free(escFrom);
				free(escFromPort);
				free(escTo);
				free(escToPort);

				chunk = NewNode(STRING);
				SetName(chunk, "Event");
				SetValueStr(chunk, buf);
				SetPropLong(chunk, "Conn", local->replyConn);
				SndMsg(instance, "Out", msg_send, chunk);
			}
		}
	}

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, "{\"event\":\"connections-done\"}");
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

void Bridge_SaveFlow(NodeObj instance, InstanceData *local, NodeObj command);
void Bridge_LoadFlow(NodeObj instance, InstanceData *local, NodeObj command);
void Bridge_ListFlows(NodeObj instance, InstanceData *local, NodeObj command);

/* the command switch itself, factored out of Bridge_OnIn so Load/Import  */
/* replay (Bridge_LoadFlow) can run a recorded instruction through the     */
/* exact same functions a live command uses - no separate replay-only      */
/* code path to keep in sync. Deliberately does not re-check RequireAuth:  */
/* that gate belongs to the wire (Bridge_OnIn), not to something already   */
/* authorized (a live command that passed it, or a session being loaded).  */
static void Bridge_Dispatch(NodeObj instance, InstanceData *local, char *cmd, NodeObj command)
{
	if (!cmd)
		Bridge_Error(instance, "unknown", "missing cmd");
	else if (strcmp(cmd, "login") == 0)
		Bridge_Login(instance, local, command);
	else if (strcmp(cmd, "create-instance") == 0)
		Bridge_Create(instance, local, command);
	else if (strcmp(cmd, "create-alias") == 0)
		Bridge_CreateAlias(instance, local, command);
	else if (strcmp(cmd, "clone-instance") == 0)
		Bridge_CloneCmd(instance, local, command);
	else if (strcmp(cmd, "move-instance") == 0)
		Bridge_Move(instance, local, command);
	else if (strcmp(cmd, "internals") == 0)
		Bridge_Internals(instance, local, command);
	else if (strcmp(cmd, "connect") == 0)
		Bridge_Connect(instance, local, command);
	else if (strcmp(cmd, "bind-port") == 0)
		Bridge_BindPort(instance, local, command);
	else if (strcmp(cmd, "disconnect") == 0)
		Bridge_Disconnect(instance, local, command);
	else if (strcmp(cmd, "bind-property") == 0)
		Bridge_BindProperty(instance, local, command);
	else if (strcmp(cmd, "bind-activate") == 0)
		Bridge_BindActivate(instance, local, command);
	else if (strcmp(cmd, "set-property") == 0)
		Bridge_Set(instance, local, command);
	else if (strcmp(cmd, "activate") == 0)
		Bridge_DoActivate(instance, local, command);
	else if (strcmp(cmd, "delete-instance") == 0)
		Bridge_Delete(instance, local, command);
	else if (strcmp(cmd, "subscribe") == 0)
		Bridge_Subscribe(instance, local, command);
	else if (strcmp(cmd, "list-instances") == 0)
		Bridge_ListInstances(instance, local, command);
	else if (strcmp(cmd, "list-connections") == 0)
		Bridge_ListConnections(instance, local);
	else if (strcmp(cmd, "save-flow") == 0)
		Bridge_SaveFlow(instance, local, command);
	else if (strcmp(cmd, "load-flow") == 0 || strcmp(cmd, "import-flow") == 0)
		Bridge_LoadFlow(instance, local, command);
	else if (strcmp(cmd, "list-flows") == 0)
		Bridge_ListFlows(instance, local, command);
	else
		Bridge_Error(instance, cmd, "unknown command");
}

/* the commands that change the shared graph - what Save/Load/Import      */
/* actually needs to reproduce (or reproduce the absence of) a session.    */
/* subscribe/list-instances/login are all just ways of looking at or       */
/* authenticating against it.                                              */
static int Bridge_IsMutating(char *cmd)
{
	return strcmp(cmd, "create-instance") == 0
		|| strcmp(cmd, "create-alias") == 0
		|| strcmp(cmd, "clone-instance") == 0
		|| strcmp(cmd, "move-instance") == 0
		|| strcmp(cmd, "connect") == 0
		|| strcmp(cmd, "bind-port") == 0
		|| strcmp(cmd, "bind-property") == 0
		|| strcmp(cmd, "bind-activate") == 0
		|| strcmp(cmd, "set-property") == 0
		|| strcmp(cmd, "activate") == 0
		|| strcmp(cmd, "disconnect") == 0
		|| strcmp(cmd, "delete-instance") == 0;
}

/* a standalone copy of command, named cmd instead of carrying "cmd" as a  */
/* field - recorded into local->flow verbatim (same field names the wire   */
/* protocol already uses), so replaying it later is just handing it back   */
/* to Bridge_Dispatch, unchanged. "Conn" comes along for the ride but is   */
/* never read back off a flow instruction, so it's harmless.               */
static NodeObj Bridge_CloneCommand(NodeObj command, char *cmd)
{
	NodeObj clone, prop;
	char *name, *value;

	clone = NewNode(INTEGER);
	SetName(clone, cmd);

	prop = GetNextProp(command);
	while (prop)
	{
		name = GetNameStr(prop);
		if (name && strcmp(name, "cmd") != 0)
		{
			value = GetValueStr(prop);
			SetPropStr(clone, name, value ? value : "");
		}
		prop = GetNextSibling(prop);
	}

	return clone;
}

/* subscription callback: parse one command, dispatch it */
int Bridge_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char *text, *cmd;
	NodeObj command;
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	/* a peer closed: take its owned (hidden helper) instances with it -  */
	/* see Bridge_ConnClosed. The alias table itself stays for the        */
	/* Bridge's whole lifetime and Conn ids are never reused, so this is  */
	/* the only per-connection state there is to clear.                    */
	if (message == msg_eof)
	{
		Bridge_ConnClosed(instance, local, GetPropLong(data, "Conn"));
		return rtrn_handled;
	}

	if (message != msg_send)
		return rtrn_dropped;

	text = GetValueStr(data);
	if (!text)
		return rtrn_dropped;

	/* remembered for the duration of this one command's handling - any   */
	/* reply it produces (an error, a targeted event) is tagged back to   */
	/* the same connection, while broadcasts to the whole shared view     */
	/* (instance-created, subscription taps) simply don't touch this      */
	local->replyConn = GetPropLong(data, "Conn");

	command = TextToProps(text);
	if (!command)
	{
		Bridge_Error(instance, "unknown", "could not parse command");
		return rtrn_handled;
	}

	cmd = GetPropStr(command, "cmd");

	if (cmd && strcmp(cmd, "login") != 0
		&& GetPropInt(instance, "RequireAuth") && !GetConnState(local->connAuth, local->replyConn))
	{
		Bridge_Error(instance, cmd, "login required");
	}
	else
	{
		Bridge_Dispatch(instance, local, cmd, command);

		/* record after dispatch, not before: a successful replay later    */
		/* needs the same command whether or not this particular one       */
		/* happened to fail (a failed Create's later Set/Connect just fail  */
		/* harmlessly too on replay, same as they did live)                 */
		if (cmd && local->flow && Bridge_IsMutating(cmd))
			AppendChild(local->flow, Bridge_CloneCommand(command, cmd));
	}

	DelNode(command);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Bridge_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* no socket of its own, nothing to schedule - Activate just goes live */
int Bridge_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/*
 * Flows live in one place: the saved/ directory next to the framework
 * (created on first save). A flow name is a bare filename - no paths,
 * no escaping the directory - and gets its .flow extension appended
 * when missing. Anything suspicious falls back to "session".
 */
static void Bridge_FlowPath(char *name, char *out, int outlen)
{
	int len;

	if (!name || !name[0] || strchr(name, '/') || strstr(name, ".."))
		name = "session";

	len = (int) strlen(name);
	if (len > 5 && strcmp(name + len - 5, ".flow") == 0)
		snprintf(out, outlen, "saved/%s", name);
	else
		snprintf(out, outlen, "saved/%s.flow", name);
}

/* one targeted JSON event back to whoever asked - the same shape        */
/* Bridge_Internals' reply already uses                                   */
static void Bridge_ReplyEvent(NodeObj instance, InstanceData *local, char *event, char *field, char *value)
{
	char buf[600];
	char *esc = JsonEscapeStr(value ? value : "");
	NodeObj chunk;

	snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"%s\":%s}", event, field, esc);
	free(esc);

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, buf);
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/* {"cmd":"save-flow","file":"myapp"} - write the session's recorded     */
/* flow to saved/myapp.flow                                               */
void Bridge_SaveFlow(NodeObj instance, InstanceData *local, NodeObj command)
{
	char path[300];

	mkdir("saved", 0755);	/* fine if it already exists */
	Bridge_FlowPath(GetPropStr(command, "file"), path, sizeof(path));
	SaveFlow(local->flow, path);
	Bridge_ReplyEvent(instance, local, "flow-saved", "file", path);
}

/*
 * Load a saved flow into the session (Import is the same operation) -
 * the objects and wiring the file records arrive alongside whatever is
 * already on the canvas, nothing is cleared. Each recorded instruction
 * replays through Bridge_Dispatch, the same function a live command
 * goes through, so every resulting instance registers and broadcasts
 * exactly like a genuine create-instance - to every connected client,
 * not just whoever asked. Each replayed instruction is ALSO appended to
 * this session's own flow log: what was loaded is part of the session
 * now, and the next Save must contain it (it used to be silently
 * dropped - Load-then-Save lost everything loaded).
 *
 * The names a flow records are the names the ORIGINAL session minted
 * (/Root/View3/Slider_1, ...). Replaying them, the server mints its own
 * fresh names (a clone-instance or an unnamed create-instance names
 * itself, and if the recorded name is already taken it must, or it would
 * shadow the live one). So a recorded command that REFERS to an earlier
 * recorded instance - a create-alias whose `of` names a slider two lines
 * up, a connect between two of them - has to be rewritten to the name
 * replay actually produced, or it binds to the wrong thing (or nothing).
 * A rename map carries recorded-name -> replayed-name across the whole
 * flow so every back-reference lands on the loaded copy.
 */
static void Bridge_RemapField(NodeObj command, char *field, NodeObj map)
{
	char *v = GetPropStr(command, field);
	char *mapped;

	if (!v || !v[0])
		return;
	mapped = GetPropStr(map, v);
	if (mapped && mapped[0])
		SetPropStr(command, field, mapped);
}

void Bridge_LoadFlow(NodeObj instance, InstanceData *local, NodeObj command)
{
	char path[300];
	NodeObj flow, instr, map;
	FILE *f;
	long size;
	char *text, *name, *recordedAs, *replayedAs;

	Bridge_FlowPath(GetPropStr(command, "file"), path, sizeof(path));

	f = fopen(path, "r");
	if (!f)
	{
		Bridge_Error(instance, "load-flow", "no such flow in saved/");
		return;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	text = malloc(size + 1);
	if (text)
	{
		fread(text, 1, size, f);
		text[size] = '\0';

		flow = TextToNode(text);
		free(text);

		if (flow)
		{
			/* recorded name (as a string key) -> the pointer to the      */
			/* replayed name string, for rewriting later back-references   */
			map = NewNode(INTEGER);

			instr = GetChild(flow);
			while (instr)
			{
				name = GetNameStr(instr);

				/* rewrite every field that NAMES an earlier instance to   */
				/* the name replay gave it - before dispatching, so the    */
				/* handler resolves the loaded copy, not a stale name (or  */
				/* a live instance from before the load that happens to    */
				/* share it)                                                */
				Bridge_RemapField(instr, "of", map);
				Bridge_RemapField(instr, "instance", map);
				Bridge_RemapField(instr, "from", map);
				Bridge_RemapField(instr, "to", map);
				Bridge_RemapField(instr, "container", map);

				/* what did this instruction ask to be called, and what    */
				/* did it end up called? create verbs stamp the final      */
				/* name back into "as" (Bridge_Create/CreateAlias) and      */
				/* clone-instance into... its result is not in the command, */
				/* so read the recorded intent before dispatch and the     */
				/* stamped result after                                     */
				recordedAs = GetPropStr(instr, "as");
				recordedAs = recordedAs && recordedAs[0] ? strdup(recordedAs) : NULL;

				Bridge_Dispatch(instance, local, name, instr);

				replayedAs = GetPropStr(instr, "as");
				if (recordedAs && replayedAs && replayedAs[0]
					&& strcmp(recordedAs, replayedAs) != 0)
					SetPropStr(map, recordedAs, replayedAs);
				if (recordedAs)
					free(recordedAs);

				if (Bridge_IsMutating(name))
					AppendChild(local->flow, Bridge_CloneCommand(instr, name));

				instr = GetNextSibling(instr);
			}

			DelNode(map);
			DelNode(flow);
		}
	}
	fclose(f);

	Bridge_ReplyEvent(instance, local, "flow-loaded", "file", path);
}

/* {"cmd":"list-flows"} - one targeted flow-file event per .flow in      */
/* saved/, then flows-done: what a client's file dialog lists            */
void Bridge_ListFlows(NodeObj instance, InstanceData *local, NodeObj command)
{
	DIR *d;
	struct dirent *e;
	int len;

	d = opendir("saved");
	if (d)
	{
		while ((e = readdir(d)) != NULL)
		{
			len = (int) strlen(e->d_name);
			if (len > 5 && strcmp(e->d_name + len - 5, ".flow") == 0)
				Bridge_ReplyEvent(instance, local, "flow-file", "file", e->d_name);
		}
		closedir(d);
	}

	Bridge_ReplyEvent(instance, local, "flows-done", "file", "");
}

/*
 * File menu action - the File MenuButton's own Selected property fans
 * straight in here (Connect(FileMenu, "Selected", WebBridge, "FileCmd"),
 * main.c), exactly the same as any property driving anything else; this
 * object doesn't know it's a "menu", it just watches a property like it
 * would watch anything it was wired to.
 *
 * The GUI now collects a filename first (its file dialog) and sends the
 * save-flow/load-flow verbs itself, so this path only fires for a bare
 * property write (a raw client, an old flow) - it maps to the same
 * handlers with the default name.
 */
int Bridge_OnFileCmd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *cmd;
	NodeObj fileMenu, command;

	if (!local || !data)
		return rtrn_dropped;

	cmd = GetValueStr(data);
	if (!cmd || !cmd[0])
		return rtrn_dropped;

	command = NewNode(INTEGER);	/* an empty command: default file */

	if (strcmp(cmd, "Save") == 0)
		Bridge_SaveFlow(instance, local, command);
	else if (strcmp(cmd, "Load") == 0 || strcmp(cmd, "Import") == 0)
		Bridge_LoadFlow(instance, local, command);

	DelNode(command);

	/* revert the button to its plain label - "File: Save" sitting there  */
	/* forever would look like a persistent mode, not a one-shot action    */
	fileMenu = (NodeObj) GetPropLong(instance, "FileMenu");
	if (fileMenu)
		SetPropStr(fileMenu, "Selected", "");

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->container = NULL;
	local->connAuth = NewNode(INTEGER);
	local->connViews = NewNode(INTEGER);
	local->flow = NewNode(INTEGER);
	SetName(local->flow, "Session");
	local->replyConn = 0;
	local->active = 0;
	local->enabled = 1;

	/* the palette is not a separate category (see Bridge_ListInstances'   */
	/* own doc comment) - it's ordinary Root instances that happen to      */
	/* already exist, so this is the one seeding step that makes that      */
	/* true: copy BuildPalette's bootstrap instances (object.c) straight   */
	/* into this Bridge's own alias table, under the exact full-path       */
	/* aliases they were built with (/Root/Palette/<Class>, and            */
	/* /Root/Palette for the View itself - no well-known short name left,  */
	/* it's an ordinary top-level View like any a user could create, see   */
	/* BuildPalette's own doc comment), before this Bridge ever answers a  */
	/* single command.                                                     */
	{
		NodeObj entry = GetNextProp(GetPalette());
		while (entry) {
			RegisterPath(GetNameStr(entry), (NodeObj) GetValueLong(entry));
			entry = GetNextSibling(entry);
		}
		RegisterPath("/Root/Palette", GetPaletteView());
	}

	instance = NewNode(INTEGER);
	SetName(instance, "Bridge");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropInt(instance, "Out", 0);		/* events leave here */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Bridge_Activate);

	/* "0" (the default): every command works unauthenticated, same as   */
	/* every Bridge before this. Set to "1" and nothing but "login"      */
	/* works until it succeeds - Main must also be set (see below) so    */
	/* login has a Users tree to check against                          */
	SetPropStr(instance, "RequireAuth", "0");
	WatchableProp(instance, "RequireAuth");

	/* raw pointer to Main, set externally after CreateObject("Bridge") - */
	/* the same "local"-style cross-reference every object already uses, */
	/* needed here only so login can find Main/Users                      */
	SetPropLong(instance, "Main", (long) NULL);

	/* same cross-reference idea, so Bridge_OnFileCmd can reset the File   */
	/* menu's own Selected back to "" once it's handled an action - set    */
	/* externally after CreateObject("Bridge"), see main.c                 */
	SetPropLong(instance, "FileMenu", (long) NULL);

	/* input port: commands arrive here, from whatever transport is wired up */
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Bridge_OnIn);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Bridge_OnEnable);

	/* File menu's Selected fans in here (Connect, main.c) - not a Bridge   */
	/* control protocol command, an ordinary property driving an ordinary   */
	/* port, same as Enable                                                 */
	SetPropInt(instance, "FileCmd", 0);
	port = GetPropNode(instance, "FileCmd");
	SetPropLong(port, "OnMsg", (long)Bridge_OnFileCmd);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		NodeObj entry, table;

		/* each conn's view table is a real allocation hanging off the   */
		/* connViews entries - free them before the table itself          */
		for (entry = GetNextProp(local->connViews); entry; entry = GetNextSibling(entry))
		{
			table = (NodeObj) GetValueLong(entry);
			if (table)
				DelNode(table);
		}
		DelNode(local->connViews);

		DelNode(local->connAuth);
		DelNode(local->flow);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Bridge");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishProp(ClassSelf, "Enable",      "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "In",          "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",         "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "State",       "data", PROP_LED, "1");
	PublishProp(ClassSelf, "RequireAuth", "data", PROP_CHECKBOX, "0");

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

	SetName(temp, "Bridge");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c690");
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
