
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    {"cmd":"bind-property","from":"Textbox1","fromPort":"Value","to":"Reader1","toProp":"Filename"}
    {"cmd":"bind-activate","from":"Button1","fromPort":"Out","to":"Reader1"}
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

bind-property and bind-activate are the same idea aimed at things
Connect() alone cannot reach: a target property chosen at runtime (not
a compiled-in port like Reader's "Enable"), or a target's Activate. Both
go through the adapter functions in object.c (ConnectToProperty /
ConnectToActivate) - real dataflow, not the Bridge quietly issuing
set-property/activate on the widget's behalf. This is what lets a
Checkbox or Button be an actual registered class in the palette (see
objects/widget, objects/button) rather than raw HTML standing in for
one: the client creates a real widget instance and wires it to whatever
it is meant to control, the same way it would wire any two objects.

Events out, on success or failure:

    {"event":"instance-created","instance":"Reader1","class":"Reader","parent":"Root","interface":{...Interface node, verbatim...}}
    {"event":"property-changed","instance":"Reader1","port":"State","value":"2"}
    {"event":"message-flowed","instance":"Reader1","port":"Out","value":"..."}
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
	NodeObj aliases;	/* alias string -> instance NodeObj, as long props - shared, not per-Conn */
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

	inst = (NodeObj) GetPropLong(local->aliases, alias);
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
 * events would keep reporting a stale path forever after a move. A linear
 * scan is fine here: it only runs when a tap actually fires, not per
 * frame, and local->aliases is not a large table.
 */
static char *Bridge_AliasForInstance(InstanceData *local, NodeObj inst)
{
	NodeObj entry;

	if (!local || !inst)
		return NULL;

	for (entry = GetNextProp(local->aliases); entry; entry = GetNextSibling(entry))
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
		if (!GetPropLong(local->aliases, out))
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
	char *classname, *alias;
	int hidden;
	NodeObj inst;

	classname = GetPropStr(command, "class");
	alias     = GetPropStr(command, "as");
	hidden    = GetPropInt(command, "hidden") != 0;

	if (!classname || !classname[0] || !alias || !alias[0])
	{
		Bridge_Error(instance, "create-instance", "class and as are required");
		return;
	}

	inst = CreateObject(local->container, classname);
	if (!inst)
	{
		Bridge_Error(instance, "create-instance", "class not found");
		return;
	}

	if (hidden)
	{
		SetPropInt(inst, "_Hidden", 1);

		/* hidden helpers are one client's chrome, not shared session   */
		/* state - remember which connection built this one so its      */
		/* close can take it along (Bridge_ConnClosed). User-built      */
		/* session objects are never stamped and survive their          */
		/* creator's browser closing, which is the point of the shared  */
		/* session.                                                      */
		if (local->replyConn)
			SetPropLong(inst, "_OwnerConn", local->replyConn);
	}

	SetPropLong(local->aliases, alias, (long) inst);
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

	inst = CreateObject(local->container, "Alias");
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
		NodeObj node;
		char *realName;

		node = ResolvePort(&owner, prop);
		realName = Bridge_AliasForInstance(local, owner);
		if (realName)
			of = realName;
		if (node)
			prop = GetNameStr(node);
	}

	SetPropStr(inst, "Target", of);
	SetPropStr(inst, "TargetProp", prop);

	/* atomic birth: named IN its container with its position, in this    */
	/* one command - never born at root and then moved, which raced every */
	/* client that addressed it by its seconds-old birth name              */
	SetOrDeliverProp(inst, "Container", container ? container : "");
	if (x && x[0]) SetOrDeliverProp(inst, "X", x);
	if (y && y[0]) SetOrDeliverProp(inst, "Y", y);

	if (hidden)
	{
		SetPropInt(inst, "_Hidden", 1);
		/* same ownership rule as Bridge_Create: hidden plumbing dies    */
		/* with the connection that made it                               */
		if (local->replyConn)
			SetPropLong(inst, "_OwnerConn", local->replyConn);
	}

	/* the server names things; a client-supplied "as" is honored when   */
	/* given (scripts), otherwise the name is minted where it lives       */
	if (!alias || !alias[0])
	{
		Bridge_FreshAlias(local, container, "Alias", fresh, sizeof(fresh));
		alias = fresh;
	}

	SetPropLong(local->aliases, alias, (long) inst);
	Bridge_SetNameFromAlias(inst, alias);
	Bridge_InstanceEvent(instance, local, alias, "Alias", GetParent(inst), "Root", GetPropStr(inst, "Container"), hidden, 0);
}

/* clone one non-Alias instance into container: engine snapshot           */
/* (CloneObject, object.c), fresh name, containment, broadcast - the      */
/* display side just sees another instance-created                        */
static NodeObj Bridge_CloneOne(NodeObj instance, InstanceData *local, NodeObj src, char *container)
{
	NodeObj class = GetParent(src), inst;
	char *classname = GetNameStr(class);
	char newAlias[256];

	inst = CloneObject(src);
	if (!inst)
		return NULL;

	SetOrDeliverProp(inst, "Container", container ? container : "");
	Bridge_FreshAlias(local, container, classname, newAlias, sizeof(newAlias));
	SetPropLong(local->aliases, newAlias, (long) inst);
	Bridge_SetNameFromAlias(inst, newAlias);
	Bridge_InstanceEvent(instance, local, newAlias, classname, class, "Root", container ? container : "", 0, 0);

	return inst;
}

/* clone an Alias member of a view being deep-cloned: if its target was   */
/* itself cloned along with the view (it's in `map`), the new alias       */
/* points at the CLONE - a self-contained panel stays self-contained.     */
/* A target outside the cloned view stays the original.                    */
static void Bridge_CloneAliasMember(NodeObj instance, InstanceData *local, NodeObj src, char *container, NodeObj map)
{
	char *propname = GetPropStr(src, "TargetProp");
	NodeObj linknode, targetInst, mapped, inst;
	char newAlias[256];
	char *v, *tn;
	int i;
	char *carry[] = { "Widget", "Label", "X", "Y" };

	if (!propname || !propname[0])
		return;

	linknode = GetPropNode(src, "Value");	/* the alias's doorway slot */
	targetInst = linknode ? (NodeObj) GetPropLong(linknode, "LinkInst") : NULL;
	if (!targetInst)
		return;

	mapped = (NodeObj) GetConnState(map, (long) targetInst);
	if (mapped)
		targetInst = mapped;

	inst = CreateObject(local->container, "Alias");
	if (!inst)
		return;

	if (!LinkPropertyAs(inst, "Value", targetInst, propname))
	{
		DeleteInstance(inst);
		return;
	}

	tn = Bridge_AliasForInstance(local, targetInst);
	SetPropStr(inst, "Target", tn ? tn : "");
	SetPropStr(inst, "TargetProp", propname);
	for (i = 0; i < 4; i++)
	{
		v = GetPropStr(src, carry[i]);
		if (v && v[0])
			SetPropStr(inst, carry[i], v);
	}
	SetPropStr(inst, "Container", container ? container : "");

	Bridge_FreshAlias(local, container, "Alias", newAlias, sizeof(newAlias));
	SetPropLong(local->aliases, newAlias, (long) inst);
	Bridge_SetNameFromAlias(inst, newAlias);
	Bridge_InstanceEvent(instance, local, newAlias, "Alias", GetParent(inst), "Root", container ? container : "", 0, 0);
}

/* deep-clone a view's members. Two full passes: everything concrete       */
/* first (filling `map` with src->clone, recursing into nested views),      */
/* then every Alias member, so an alias can be remapped no matter where     */
/* in the tree its target's clone landed. Member lists are snapshotted      */
/* before cloning so the entries the clones themselves add to the session   */
/* are never re-walked mid-pass.                                             */
static void Bridge_CloneViewMembers(NodeObj instance, InstanceData *local, char *srcViewAlias, char *newViewAlias, NodeObj map, int aliasPass)
{
	NodeObj list, entry, member, clone;
	char *name, *cont, *classname;

	list = NewNode(INTEGER);
	for (entry = GetNextProp(local->aliases); entry; entry = GetNextSibling(entry))
	{
		name = GetNameStr(entry);
		member = (NodeObj) GetValueLong(entry);
		if (!member || !name)
			continue;
		if ((NodeObj) GetPropLong(local->aliases, name) != member)
			continue;	/* stale shadow - see Bridge_ConnClosed */
		if (GetPropInt(member, "_Hidden"))
			continue;	/* per-connection plumbing is not content */
		cont = GetPropStr(member, "Container");
		if (!cont || strcmp(cont, srcViewAlias) != 0)
			continue;
		SetPropLong(list, name, (long) member);
	}

	for (entry = GetNextProp(list); entry; entry = GetNextSibling(entry))
	{
		name = GetNameStr(entry);
		member = (NodeObj) GetValueLong(entry);
		classname = GetNameStr(GetParent(member));

		if (strcmp(classname, "Alias") == 0)
		{
			if (aliasPass)
				Bridge_CloneAliasMember(instance, local, member, newViewAlias, map);
		}
		else if (!aliasPass)
		{
			clone = Bridge_CloneOne(instance, local, member, newViewAlias);
			if (clone)
				SetConnState(map, (long) member, (long) clone);
		}

		/* nested views recurse on both passes - in the alias pass the    */
		/* child's clone (made in pass one) is looked up through the map  */
		if (strcmp(classname, "View") == 0)
		{
			clone = (NodeObj) GetConnState(map, (long) member);
			if (clone)
			{
				char *cn = Bridge_AliasForInstance(local, clone);
				if (cn)
					Bridge_CloneViewMembers(instance, local, name, cn, map, aliasPass);
			}
		}
	}

	DelNode(list);
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
	if (!(existing && existing[0] && GetPropLong(local->aliases, existing)))
	{
		view = CreateObject(local->container, "View");
		if (!view)
		{
			Bridge_Error(instance, "internals", "View class not loaded");
			return;
		}

		/* named after the thing it belongs to */
		slash = strrchr(curAlias, '/');
		snprintf(base, sizeof(base), "%sPanel", slash ? slash + 1 : curAlias);
		Bridge_FreshAlias(local, "/Root", base, viewAlias, sizeof(viewAlias));

		/* a plumbing view: no icon of its own on anyone's canvas - the   */
		/* OBJECT's icon is its presence; this is the panel behind it     */
		SetPropInt(view, "_Hidden", 1);
		SetPropStr(view, "_InternalsOf", curAlias);
		SetOrDeliverProp(view, "Container", "");
		SetPropInt(view, "PanelX", 320);
		SetPropInt(view, "PanelY", 120);
		SetPropInt(view, "W", 300);

		SetPropLong(local->aliases, viewAlias, (long) view);
		Bridge_SetNameFromAlias(view, viewAlias);
		Bridge_InstanceEvent(instance, local, viewAlias, "View", GetParent(view), "Root", "", 1, 0);

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

			member = CreateObject(local->container, "Alias");
			if (!member)
				continue;
			if (!LinkPropertyAs(member, "Value", inst, name))
			{
				DeleteInstance(member);
				continue;
			}

			SetPropStr(member, "Target", curAlias);
			SetPropStr(member, "TargetProp", name);
			SetPropStr(member, "Container", viewAlias);
			SetPropInt(member, "X", 14);
			SetPropInt(member, "Y", 12 + row * 44);
			row++;

			Bridge_FreshAlias(local, viewAlias, "Alias", memberAlias, sizeof(memberAlias));
			SetPropLong(local->aliases, memberAlias, (long) member);
			Bridge_SetNameFromAlias(member, memberAlias);
		}

		snprintf(num, sizeof(num), "%d", 50 + row * 44);
		SetOrDeliverProp(view, "H", num);

		SetPropStr(inst, "_Internals", viewAlias);
		existing = GetPropStr(inst, "_Internals");
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
 * - the whole clone is a node operation inside the engine; the display
 * just updates from the instance-created broadcasts. Cloning an alias
 * clones the THING it stands for (a snapshot of the target's data);
 * cloning a view clones the container AND everything in it, with
 * intra-view aliases remapped onto the clones.
 */
void Bridge_CloneCmd(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *of, *container, *x, *y;
	NodeObj src, inst, map, linknode;
	char *classname, *newAlias;
	char panelPos[16];

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

	classname = GetNameStr(GetParent(src));

	inst = Bridge_CloneOne(instance, local, src, container);
	if (!inst)
	{
		Bridge_Error(instance, "clone-instance", "clone failed");
		return;
	}

	if (x && x[0]) SetOrDeliverProp(inst, "X", x);
	if (y && y[0]) SetOrDeliverProp(inst, "Y", y);

	if (strcmp(classname, "View") == 0)
	{
		/* the clone's panel would sit exactly on the source's - nudge it */
		snprintf(panelPos, sizeof(panelPos), "%d", GetPropInt(src, "PanelX") + 24);
		SetOrDeliverProp(inst, "PanelX", panelPos);
		snprintf(panelPos, sizeof(panelPos), "%d", GetPropInt(src, "PanelY") + 24);
		SetOrDeliverProp(inst, "PanelY", panelPos);

		newAlias = Bridge_AliasForInstance(local, inst);
		if (newAlias)
		{
			map = NewNode(INTEGER);
			SetConnState(map, (long) src, (long) inst);
			Bridge_CloneViewMembers(instance, local, of, newAlias, map, 0);
			Bridge_CloneViewMembers(instance, local, of, newAlias, map, 1);
			DelNode(map);
		}
	}
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
		Bridge_Error(instance, "connect", "connect failed");
}

/* {"cmd":"bind-property","from":widgetAlias,"fromPort":"Value",       */
/*  "to":targetAlias,"toProp":"Filename"} - wires an input widget's    */
/* edited value into an arbitrary property on the real target, via the */
/* adapter in object.c (Connect() alone cannot aim at a property whose */
/* name is only known at runtime)                                     */
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

	if (!ConnectToProperty(fromInst, fromPort, toInst, toProp))
		Bridge_Error(instance, "bind-property", "bind failed");
}

/* {"cmd":"bind-activate","from":buttonAlias,"fromPort":"Out",         */
/*  "to":targetAlias} - wires a Button's press to an arbitrary         */
/* target's ActivateInstance, the same adapter idea as bind-property   */
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

	if (!ConnectToActivate(fromInst, fromPort, toInst))
		Bridge_Error(instance, "bind-activate", "bind failed");
}

/*
 * An alias is a full path (see BuildPalette/CreateInstance's own naming
 * comments) - it names where an instance currently lives, not just what
 * it's called, so moving it to a different Container really does mean a
 * different alias from here on, not a rename of a fixed identity. This is
 * the one place that happens: after the Container write lands, the old
 * local->aliases entry is retired (zeroed, same as Bridge_Delete does -
 * local->aliases is a plain prop chain, not a child list, so there is no
 * DelSibling here) and a new one takes over, then every connected client
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

	SetPropLong(local->aliases, oldAlias, 0);
	SetPropLong(local->aliases, newAlias, (long) inst);

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

	if (GetPropLong(local->aliases, newAlias))
	{
		Bridge_SetNameFromAlias(inst, oldAlias);	/* put it back */
		Bridge_Error(instance, "set-property", "that name is already taken here");
		return;
	}

	SetPropLong(local->aliases, oldAlias, 0);
	SetPropLong(local->aliases, newAlias, (long) inst);

	escFrom = JsonEscapeStr(oldAlias);
	escTo   = JsonEscapeStr(newAlias);
	snprintf(buf, sizeof(buf), "{\"event\":\"instance-renamed\",\"from\":%s,\"to\":%s}", escFrom, escTo);
	free(escFrom);
	free(escTo);

	/* a pure rename is only visible where the thing lives */
	if (strcmp(oldCont, "/Root") == 0)
		oldCont[0] = 0;
	Bridge_SendEventScoped(instance, local, buf, oldCont);
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

	SetOrDeliverProp(inst, prop, value);

	/* Name and Container are ordinary properties whose value happens to  */
	/* BE the thing's identity/location - writing either re-keys the       */
	/* alias. Judged on the RESOLVED property (a dissection-table member's */
	/* "Value" slot may stand for the target's Name), and applied to the    */
	/* resolved owner: setting them through a member renames/moves the      */
	/* THING, never the member.                                              */
	{
		NodeObj owner = inst;
		NodeObj node = ResolvePort(&owner, prop);
		char *realProp = node ? GetNameStr(node) : prop;
		char *ownAlias;

		if (strcmp(realProp, "Container") == 0 || strcmp(realProp, "Name") == 0)
		{
			ownAlias = Bridge_AliasForInstance(local, owner);
			if (ownAlias)
			{
				if (strcmp(realProp, "Container") == 0)
					Bridge_Rename(instance, local, ownAlias, owner, value);
				else
					Bridge_RenameName(instance, local, ownAlias, owner, value);
			}
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
 * local->aliases directly rather than Bridge_ResolveAlias, since Chrome
 * (the topbar's File/Mode menus - real app UI, not a session object) is
 * never a delete-instance target. The Palette's own bootstrap instances
 * ARE reachable through local->aliases now (they're ordinary Root
 * instances - see InstanceStart's seeding step and Bridge_ListInstances'
 * doc comment), so what stops them from being deleted is the same
 * Deletable="0" property BuildPalette (object.c) already set on them -
 * an ordinary property check, not a structural exclusion.
 *
 * The alias entry is zeroed rather than removed - local->aliases is a
 * plain prop chain (AddProp, not AddChild), which DelSibling can't
 * unlink (see its own doc comment, node.c); a value of 0 is exactly
 * what Bridge_ResolveAlias already treats as "not found", so this is
 * enough to make the alias unresolvable without touching node shape,
 * the same pragmatic non-cleanup UnregisterLibrary already accepts.
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

void Bridge_Delete(NodeObj instance, InstanceData *local, NodeObj command)
{
	char *alias, *escAlias, *deletable;
	NodeObj inst;
	char buf[256];

	alias = GetPropStr(command, "instance");
	inst  = alias ? (NodeObj) GetPropLong(local->aliases, alias) : NULL;

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
		char cont[256];
		char *c = GetPropStr(inst, "Container");
		snprintf(cont, sizeof(cont), "%s", c ? c : "");

		Bridge_FreeTaps(inst);
		SetPropLong(local->aliases, alias, 0);
		DeleteInstance(inst);

		escAlias = JsonEscapeStr(alias);
		snprintf(buf, sizeof(buf), "{\"event\":\"instance-removed\",\"instance\":%s}", escAlias);
		free(escAlias);

		/* only the connections looking at where it lived */
		Bridge_SendEventScoped(instance, local, buf, cont);
	}
}

/*
 * A connection died (msg_eof arrived on In, tagged with its Conn id -
 * forwarded up from TCP through WebSocket, or straight from TCP on the
 * raw-JSON bridges; Conn 0 means the transport itself shut down, every
 * peer at once): delete every instance that connection owned. Only
 * hidden helper widgets ever carry _OwnerConn (Bridge_Create), and the
 * client rebuilds its helpers from scratch on its next page load
 * anyway - before this, every reconnect left a full set of them behind
 * forever, the memory growth visible on each browser reload.
 *
 * Each deletion also compacts the flow log: every recorded command that
 * mentions the dead helper's alias is removed, so the session record
 * (and session.flow on the next Save) stays the size of what actually
 * exists instead of accreting one dead create-instance per page load
 * forever. Compacting rather than appending a delete-instance is safe
 * here precisely because a swept instance was created live through
 * Bridge_OnIn (that's where _OwnerConn comes from), so its create is
 * guaranteed to be in this same log - not true of instances that
 * arrived via Load, which is why Bridge_Delete still records deletes.
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
	NodeObj entry, next, inst;
	long owner;
	char *alias, *escAlias;
	char buf[256];

	entry = GetNextProp(local->aliases);
	while (entry)
	{
		next = GetNextSibling(entry);

		alias = GetNameStr(entry);
		inst  = (NodeObj) GetValueLong(entry);

		/* live entries only: zeroing an alias prepends a shadow (see    */
		/* Bridge_Delete), so an entry only counts while it is still     */
		/* what a lookup of its own name resolves to - anything older    */
		/* holds a stale pointer that must not be dereferenced           */
		if (inst && alias && (NodeObj) GetPropLong(local->aliases, alias) == inst)
		{
			owner = GetPropLong(inst, "_OwnerConn");
			if (owner && (connId == 0 || owner == connId))
			{
				char cont[256];
				char *c = GetPropStr(inst, "Container");
				snprintf(cont, sizeof(cont), "%s", c ? c : "");

				Bridge_FreeTaps(inst);
				SetPropLong(local->aliases, alias, 0);
				DeleteInstance(inst);

				escAlias = JsonEscapeStr(alias);
				snprintf(buf, sizeof(buf), "{\"event\":\"instance-removed\",\"instance\":%s}", escAlias);
				free(escAlias);

				/* only the connections looking at where it lived */
				Bridge_SendEventScoped(instance, local, buf, cont);

				Bridge_CompactFlow(local, alias);
			}
		}

		entry = next;
	}

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
	char buf[900];

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

	snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"instance\":%s,\"port\":%s,\"value\":%s}",
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
		Bridge_Error(instance, "subscribe", "unknown instance or missing port");
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
	NodeObj entry, inst, class, chunk;
	char *container, *cont;

	container = command ? GetPropStr(command, "container") : NULL;
	if (!container)
		container = "";

	/* asking to list a container IS looking at it - from here on this   */
	/* connection receives live events about it, and only about places    */
	/* it has looked                                                       */
	Bridge_MarkViewing(local, local->replyConn, container);

	if (!container[0])
	{
		entry = GetNextProp(GetChrome());
		while (entry)
		{
			inst = (NodeObj) GetValueLong(entry);
			class = inst ? GetParent(inst) : NULL;

			if (inst && class)
				Bridge_InstanceEvent(instance, local, GetNameStr(entry), GetNameStr(class), class, "Chrome", GetPropStr(inst, "Container"), 0, local->replyConn);

			entry = GetNextSibling(entry);
		}
	}

	entry = GetNextProp(local->aliases);
	while (entry)
	{
		inst = (NodeObj) GetValueLong(entry);
		class = inst ? GetParent(inst) : NULL;

		if (inst && class)
		{
			cont = GetPropStr(inst, "Container");
			if (strcmp(cont ? cont : "", container) == 0)
				Bridge_InstanceEvent(instance, local, GetNameStr(entry), GetNameStr(class), class, "Root", cont, GetPropInt(inst, "_Hidden"), local->replyConn);
		}

		entry = GetNextSibling(entry);
	}

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, "{\"event\":\"instances-done\"}");
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/* targeted, one per genuine Connect() the flow ever recorded (see          */
/* Bridge_OnIn) - not bind-property/bind-activate, which wire a control      */
/* into an arbitrary property or an Activate rather than a named port with   */
/* a dot on screen to anchor a line to; that plumbing is already visible as   */
/* the control itself sitting in the card, no separate wire needed to        */
/* explain it. What a client asks for on entering Connect mode (app.js) -    */
/* "we draw all connections" means all of THESE, sourced straight from the    */
/* same recording Save/Load already use rather than a live graph walk, so     */
/* there is nothing new to keep in sync.                                      */
void Bridge_ListConnections(NodeObj instance, InstanceData *local)
{
	NodeObj instr, chunk;
	char *from, *fromPort, *to, *toPort, *escFrom, *escFromPort, *escTo, *escToPort;
	char buf[512];

	instr = GetChild(local->flow);
	while (instr)
	{
		if (CmpName(instr, "connect"))
		{
			from     = GetPropStr(instr, "from");
			fromPort = GetPropStr(instr, "fromPort");
			to       = GetPropStr(instr, "to");
			toPort   = GetPropStr(instr, "toPort");

			escFrom     = JsonEscapeStr(from ? from : "");
			escFromPort = JsonEscapeStr(fromPort ? fromPort : "");
			escTo       = JsonEscapeStr(to ? to : "");
			escToPort   = JsonEscapeStr(toPort ? toPort : "");

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

		instr = GetNextSibling(instr);
	}

	chunk = NewNode(STRING);
	SetName(chunk, "Event");
	SetValueStr(chunk, "{\"event\":\"connections-done\"}");
	SetPropLong(chunk, "Conn", local->replyConn);
	SndMsg(instance, "Out", msg_send, chunk);
}

/* the command switch itself, factored out of Bridge_OnIn so Load/Import  */
/* replay (Bridge_OnFileCmd) can run a recorded instruction through the    */
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
	else if (strcmp(cmd, "internals") == 0)
		Bridge_Internals(instance, local, command);
	else if (strcmp(cmd, "connect") == 0)
		Bridge_Connect(instance, local, command);
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
		|| strcmp(cmd, "connect") == 0
		|| strcmp(cmd, "bind-property") == 0
		|| strcmp(cmd, "bind-activate") == 0
		|| strcmp(cmd, "set-property") == 0
		|| strcmp(cmd, "activate") == 0
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
 * File menu action - the File MenuButton's own Selected property fans
 * straight in here (Connect(FileMenu, "Selected", WebBridge, "FileCmd"),
 * main.c), exactly the same as any property driving anything else; this
 * object doesn't know it's a "menu", it just watches a property like it
 * would watch anything it was wired to.
 *
 * Save/Load/Import all lean on local->flow, the exact recording every
 * mutating command already appends to (Bridge_OnIn) - Save just writes
 * it out; Load/Import read it back and replay each instruction through
 * Bridge_Dispatch, the same function a live command goes through, so
 * every resulting instance is registered into local->aliases and
 * broadcast (Conn 0) exactly like a genuine create-instance would be -
 * to every connected client, not just whoever clicked the menu.
 *
 * One fixed filename for now - a save-as prompt is a client-side
 * feature (a Textbox wired to this the same way everything else is)
 * that hasn't been asked for yet. Load and Import are currently the
 * same operation (replay into the live session) - a real distinction
 * would mean clearing the shared session first for Load, which is a
 * separate, genuinely destructive feature nobody has asked for.
 */
int Bridge_OnFileCmd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *cmd, *filename = "session.flow";
	NodeObj fileMenu, flow, instr;
	FILE *f;
	long size;
	char *text;

	if (!local || !data)
		return rtrn_dropped;

	cmd = GetValueStr(data);
	if (!cmd || !cmd[0])
		return rtrn_dropped;

	if (strcmp(cmd, "Save") == 0)
	{
		SaveFlow(local->flow, filename);
	}
	else if (strcmp(cmd, "Load") == 0 || strcmp(cmd, "Import") == 0)
	{
		f = fopen(filename, "r");
		if (f)
		{
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
					instr = GetChild(flow);
					while (instr)
					{
						Bridge_Dispatch(instance, local, GetNameStr(instr), instr);
						instr = GetNextSibling(instr);
					}
					DelNode(flow);
				}
			}
			fclose(f);
		}
	}

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

	local->aliases = NewNode(INTEGER);
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
			SetPropLong(local->aliases, GetNameStr(entry), GetValueLong(entry));
			entry = GetNextSibling(entry);
		}
		SetPropLong(local->aliases, "/Root/Palette", (long) GetPaletteView());
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

		DelNode(local->aliases);
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
