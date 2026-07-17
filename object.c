#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"
#include "callback.h"
#include "sched.h"

NodeObj RegObjList;

void
ObjSetRegObjList(NodeObj node){
	RegObjList = node;
}

/* The scheduler task list lives in main, but loaded objects need to */
/* schedule work.  This lives here in the shared library so main and */
/* every loaded object see the same list, just like RegObjList.      */

void * ObjTaskList;

void
ObjSetTaskList(void * list){
	ObjTaskList = list;
}

void *
ObjGetTaskList(void){
	return ObjTaskList;
}


/* find a registered library by the same Name every _init() already gives  */
/* it - the same lookup a Dependencies entry needs to resolve against      */
static NodeObj FindLibraryByName(char *name)
{
	NodeObj library = GetChild(RegObjList);
	while (library) {
		if (CmpName(library, name))
			return library;
		library = GetNextSibling(library);
	}
	return NULL;
}

/* true once every name in library's own comma-separated "Dependencies"    */
/* property (empty/unset counts as none) is itself already marked Loaded - */
/* see loadClasses below. A dependency naming a library that was never      */
/* scanned/loaded at all is simply never satisfied; loadClasses' fallback    */
/* pass is what keeps that from hanging the boot forever.                   */
static int DependenciesReady(NodeObj library)
{
	char *deps, *dep, buf[256];
	NodeObj depLib;

	deps = GetPropStr(library, "Dependencies");
	if (!deps || !deps[0])
		return 1;

	strncpy(buf, deps, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	dep = strtok(buf, ",");
	while (dep) {
		depLib = FindLibraryByName(dep);
		if (!depLib || !GetPropInt(depLib, "Loaded"))
			return 0;
		dep = strtok(NULL, ",");
	}
	return 1;
}

/*
 * Dependency-ordered class bring-up: every library scanned/dlopen'd by
 * InstallObjects() is sitting in RegObjList by now (see main.c), each
 * carrying whatever "Dependencies" its own _init() declared (a comma-
 * separated list of other libraries' Name, empty by default - see
 * object.h's PropertyType-adjacent doc comment near RegisterLibrary).
 * This repeatedly sweeps the list, calling ClassStart on whichever
 * libraries have all their dependencies already Loaded, until nothing is
 * left. A pass that makes no progress means an unresolved or circular
 * dependency - rather than refuse to boot, the remaining libraries just
 * load in registration order, same as before this existed, with a single
 * warning; a best-effort ordering pass, not a hard requirement to start.
 */
loadClasses(){
	NodeObj library;
	int madeProgress, remaining;
	msgobj ClassStart;

	remaining = 0;
	library = GetChild(RegObjList);
	while (library) {
		SetPropInt(library, "Loaded", 0);
		remaining++;
		library = GetNextSibling(library);
	}

	while (remaining > 0) {
		madeProgress = 0;

		library = GetChild(RegObjList);
		while (library) {
			if (!GetPropInt(library, "Loaded") && DependenciesReady(library)) {
				ClassStart = (msgobj) GetPropLong(library, "ClassStart");
				if (ClassStart) ClassStart(library, 0, NULL);
				SetPropInt(library, "Loaded", 1);
				remaining--;
				madeProgress = 1;
			}
			library = GetNextSibling(library);
		}

		if (!madeProgress && remaining > 0) {
			DebugPrint("Unresolved or circular library Dependencies - loading the rest in registration order", __FILE__, __LINE__, ERROR);
			library = GetChild(RegObjList);
			while (library) {
				if (!GetPropInt(library, "Loaded")) {
					ClassStart = (msgobj) GetPropLong(library, "ClassStart");
					if (ClassStart) ClassStart(library, 0, NULL);
					SetPropInt(library, "Loaded", 1);
				}
				library = GetNextSibling(library);
			}
			remaining = 0;
		}
	}
}

UnloadClasses(){
	NodeObj library = GetChild( RegObjList );
	while (library) {
		msgobj ClassEnd = (msgobj)GetPropLong(library, "ClassEnd");
		if (ClassEnd) ClassEnd(library, 0, NULL);
		library = GetNextSibling(library);
	}
}



		//printf ("In core:     Class callbacks: %lu, %lu, %lu\n", (long)ClassStart, (long)ClassEnd, (long)ClassMsg);
		//msgobj ClassEnd   = (msgobj)GetPropLong(library, "ClassEnd");
		//msgobj ClassMsg   = (msgobj)GetPropLong(library, "ClassMsg");
		//PrintNode(library);

NodeObj
CreateContainer(NodeObj container, char * name){

        // these containers just exist in our nodes to organize groups of objects together.
        // evolve into an application grouping with a schedule? 

        // these could be functional organizations
        // later we could also have these same objects in multiple views in logical organizations


        // need to check to see if name already exists
	NodeObj temp = NewNode(INTEGER);
	SetName(temp, name);

	AddChild(container, temp);

	return temp;
}

/*
 * Users as nodes: Main/Users/<name>, each with its own Canvas container
 * (via CreateContainer - it does not mean anything more than any other
 * container yet, ready for Phase 5's container ports to make it real).
 * Token auth only for now - the roadmap explicitly sequences TLS after
 * this, and TLS means linking OpenSSL, a real new dependency this build
 * does not have; the VNOS reference TCPObject.c has the SSL_* call
 * shape to port when that happens.
 */
NodeObj CreateUser(NodeObj main, char *name, char *token){

	NodeObj users, user;

	if (!main || !name)
		return NULL;

	users = GetPropNode(main, "Users");
	if (!users) {
		users = NewNode(INTEGER);
		SetName(users, "Users");
		AddProp(main, users);
	}

	user = NewNode(INTEGER);
	SetName(user, name);
	SetPropStr(user, "Token", token ? token : "");
	AppendChild(users, user);

	CreateContainer(user, "Canvas");

	return user;
}

NodeObj FindUser(NodeObj main, char *name){

	NodeObj users, user;

	users = main ? GetPropNode(main, "Users") : NULL;
	if (!users || !name)
		return NULL;

	user = GetChild(users);
	while (user) {
		if (CmpName(user, name))
			return user;
		user = GetNextSibling(user);
	}
	return NULL;
}

/* the user node on success, NULL on an unknown user or a token mismatch */
NodeObj AuthenticateUser(NodeObj main, char *name, char *token){

	NodeObj user;
	char *stored;

	user = FindUser(main, name);
	if (!user)
		return NULL;

	stored = GetPropStr(user, "Token");
	if (!stored || !token || strcmp(stored, token) != 0)
		return NULL;

	return user;
}

/* the registry root - RegObjList -> libraries -> classes - for anything */
/* that needs to walk every class (a palette) rather than find one by    */
/* name; GetChild/GetNextSibling at both levels is all that takes        */
NodeObj GetRegObjList(void){
	return RegObjList;
}

static NodeObj Palette;
static NodeObj PaletteView;

/*
 * The palette is not a special client-side concept - it is a real View
 * instance (PaletteView, living at the top level like any View a user
 * could create themselves - its current alias is /Root/Palette, no
 * well-known short name, see Bridge's InstanceStart seeding step), holding
 * one bootstrap
 * instance of every registered class as ordinary children (their own
 * Container property names this View, same as dragging anything else into
 * it would set). Two property VALUES are all that make it behave
 * differently from any other View: Deletable="0" (on the View itself and
 * on every one of its bootstrap children - you can't delete the catalog
 * out from under yourself) and Mode="Clone" (see view.c's own doc comment)
 * - interacting with anything inside it clones instead of following the
 * session's own current mode. Nothing else is special: drag things around
 * inside it, add to it, edit the bootstrap instances' properties, and
 * Save/Load carries all of it exactly the way it carries any other View's
 * contents, because it IS just a View.
 *
 * GetPalette() keeps returning the class-name -> instance lookup table
 * (unchanged shape) - Bridge (bridge.c) is the only remaining caller,
 * seeding its own alias table from it once at startup so these become
 * ordinary, addressable Root instances with no separate wire-protocol
 * category at all. GetPaletteView() is the View itself.
 *
 * Every bootstrap instance's alias is its current full path
 * (/Root/Palette/<Class>) - there is no separate "creation path" concept,
 * only a current one, the same convention app.js's createInstance() uses
 * for anything a client creates (/Root/<Class><N>). See Bridge_Set's
 * Bridge_Rename for what happens when something later moves to a
 * different Container: the alias changes with it, it isn't a permanent
 * identity independent of where the thing actually lives.
 *
 * Built once, after loadClasses() has run so every class actually exists
 * to instantiate (see main.c). None of the bootstrap instances are ever
 * Activated - they exist to be inspected and cloned from, the same way a
 * catalog entry is looked at, not run.
 */
NodeObj GetPalette(void){
	return Palette;
}

NodeObj GetPaletteView(void){
	return PaletteView;
}

/* Bridge and TCP are the transport carrying this very session, not      */
/* something a user composing a dataflow should drag out and rewire -    */
/* the same reason a web page builder doesn't let you drag out "an HTTP  */
/* connection." Not an architectural exclusion - nothing stops either    */
/* from being Connect()ed to or built into a flow file like anything     */
/* else - just a curation choice about what belongs in the palette a     */
/* user actually composes with.                                         */
static int IsPaletteExcluded(char *className)
{
	return strcmp(className, "Bridge") == 0
		|| strcmp(className, "TCP") == 0
		/* an Alias only means something bound to a target - they are    */
		/* born from the Alias-mode drag (create-alias), never dragged    */
		/* out of the palette as a blank                                   */
		|| strcmp(className, "Alias") == 0;
}

void BuildPalette(void){

	NodeObj library, class, inst;
	int slot;
	char alias[128];

	/* no forced Mode, no Deletable protection: the palette is just a     */
	/* View like Root or any panel, and everything in it deletes like     */
	/* anything else - a restart rebuilds it, so nothing here is precious. */
	/* X/Y position the icon; PanelX/PanelY position its open panel        */
	/* (independent - the icon never goes away).                            */
	PaletteView = CreateObject(NULL, "View");
	SetPropStr(PaletteView, "Name", "Palette");
	SetPropStr(PaletteView, "Open", "1");	/* views default closed; the palette starts open */
	SetPropInt(PaletteView, "X", 20);
	SetPropInt(PaletteView, "Y", 20);
	SetPropInt(PaletteView, "PanelX", 20);
	SetPropInt(PaletteView, "PanelY", 60);
	SetPropInt(PaletteView, "W", 190);
	SetPropInt(PaletteView, "H", 220);	/* the inner area scrolls; resize to taste */

	Palette = NewNode(INTEGER);
	SetName(Palette, "Palette");

	/* every bootstrap instance defaults to X=0,Y=0 (InitPosition) - left    */
	/* alone they'd all stack exactly on top of each other inside the        */
	/* Palette's inner area. A simple two-column grid, just so there is       */
	/* something to look at on first boot - completely ordinary X/Y writes,   */
	/* the user can drag any of them anywhere else afterward like anything    */
	/* else in a View.                                                        */
	slot = 0;

	library = GetChild(RegObjList);
	while (library)
	{
		class = GetChild(library);
		while (class)
		{
			if (!IsPaletteExcluded(GetNameStr(class)))
			{
				inst = CreateObject(NULL, GetNameStr(class));
				if (inst) {
					SetPropStr(inst, "Container", "/Root/Palette");
					SetPropStr(inst, "Name", GetNameStr(class));
					SetPropInt(inst, "X", 10 + (slot % 2) * 80);
					SetPropInt(inst, "Y", 10 + (slot / 2) * 66);
					slot++;

					/* the alias is this instance's full path, same         */
					/* convention as a client-created instance's /Root/...   */
					/* (createInstance, app.js) - see the doc comment above  */
					/* and Bridge_Set's Bridge_Rename for what happens if    */
					/* it ever moves out of here later.                      */
					snprintf(alias, sizeof(alias), "/Root/Palette/%s", GetNameStr(class));
					SetPropLong(Palette, alias, (long) inst);
				}
			}

			class = GetNextSibling(class);
		}
		library = GetNextSibling(library);
	}
}

static NodeObj Chrome;

/*
 * The app's own topbar chrome (File menu, Mode menu) is not a special
 * client-side concept - it's a small, fixed set of real instances,
 * discovered and addressed exactly the same way the Palette is (a long
 * prop per well-known name -> live NodeObj pointer). "Eat our own dog
 * food": these are MenuButton instances like any a user could drag out
 * of the palette for their own app; the only thing that marks them as
 * chrome is which group Bridge_ListInstances reports them under.
 *
 * Built once, after BuildPalette - MenuButton has to already be a
 * registered class.
 */
NodeObj GetChrome(void){
	return Chrome;
}

void BuildChrome(void){

	NodeObj fileMenu, modeMenu;

	Chrome = NewNode(INTEGER);
	SetName(Chrome, "Chrome");

	fileMenu = CreateObject(NULL, "MenuButton");
	if (fileMenu) {
		SetPropStr(fileMenu, "Label", "File");
		SetPropStr(fileMenu, "Items", "Load,Save,Import");
		SetPropLong(Chrome, "FileMenu", (long) fileMenu);
	}

	modeMenu = CreateObject(NULL, "MenuButton");
	if (modeMenu) {
		SetPropStr(modeMenu, "Label", "Mode");
		SetPropStr(modeMenu, "Items", "Operate,Clone,Alias,Move,Connect,Delete,Options");
		SetPropStr(modeMenu, "Selected", "Operate");
		SetPropLong(Chrome, "ModeMenu", (long) modeMenu);
	}
}

/* walk the registry looking for a registered class by name */
/* the registry is RegObjList -> libraries -> classes        */
NodeObj
FindClass(char * classname){

	NodeObj library = GetChild(RegObjList);
	NodeObj class;

	while (library) {
		class = GetChild(library);
		while (class) {
			if (CmpName(class, classname))
				return class;
			class = GetNextSibling(class);
		}
		library = GetNextSibling(library);
	}
	return NULL;
}

NodeObj
CreateObject(NodeObj container, char * classname){

	NodeObj class;
	msgobj InstanceStart;

	class = FindClass(classname);
	if (!class) {
		DebugPrint ( "CreateObject could not find a registered class by that name.", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	if (!InstanceStart) {
		DebugPrint ( "CreateObject found a class with no InstanceStart.", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	/* the class creates and registers the instance itself, */
	/* RegisterInstance leaves it in LastInstance for us     */
	InstanceStart(class, msg_initialize, NULL);

	/* the instance lives in the registry under its class,      */
	/* the container is not a second parent in the node tree    */

	return (NodeObj)GetPropLong(class, "LastInstance");
}

/*
 * The alias mechanism's one moving part: resolve (instance, propname)
 * through any link chain to the pair that actually owns the property.
 * A linked prop node (LinkNode, node.c) carries a LinkInst prop naming
 * the owning instance (set by LinkProperty below, which collapses
 * chains at creation), so the caller gets back both the real prop node
 * AND the real instance - handlers need the owning instance to find
 * their "local" state, so resolving the node alone is not enough.
 * For a plain unlinked property this is exactly GetPropNode.
 */
NodeObj ResolvePort(NodeObj * instp, char * name)
{
	NodeObj raw, owner;

	if (!instp || !*instp || !name)
		return NULL;

	raw = GetPropNode(*instp, name);
	if (!raw)
		return NULL;

	if (GetNodeLink(raw))
	{
		owner = (NodeObj) GetPropLong(raw, "LinkInst");
		if (owner)
			*instp = owner;
		return ResolveNode(raw);
	}

	return raw;
}

/*
 * Expose targetInst's property on owner under `slot`, as a link.
 * Everything that resolves ports (Connect, SndMsg, SetOrDeliverProp,
 * the Bridge's subscribe) lands on the original: one value, one
 * subscriber list, no forwarding. Aliasing an alias collapses to the
 * final original at creation, so chains never grow at use time.
 *
 * The slot matters: an Alias instance keeps the link in its own "Value"
 * slot precisely so its OWN Name/Container/X/Y stay its own - linking a
 * target's Name under the name "Name" would hijack the alias's identity
 * (renaming the member renamed the target, and vice versa).
 */
int LinkPropertyAs(NodeObj owner, char * slot, NodeObj targetInst, char * propname)
{
	NodeObj targetProp, linknode, realOwner;

	if (!owner || !slot || !targetInst || !propname)
		return 0;

	realOwner = targetInst;
	targetProp = ResolvePort(&realOwner, propname);
	if (!targetProp)
		return 0;

	SetPropStr(owner, slot, "");
	linknode = GetPropNode(owner, slot);
	if (!linknode)
		return 0;

	SetPropLong(linknode, "LinkInst", (long) realOwner);
	LinkNode(linknode, targetProp);

	return 1;
}

/* same, exposed under the target property's own name */
int LinkProperty(NodeObj owner, NodeObj targetInst, char * propname)
{
	return LinkPropertyAs(owner, propname, targetInst, propname);
}

/*
 * The engine-level clone: a brand-new instance of source's class
 * carrying a snapshot of source's published data properties - its own
 * separate copy, nothing shared, nothing linked. Reading each value
 * through ResolvePort means cloning THROUGH an alias never copies the
 * alias's plumbing - it snapshots the original's live values. Naming,
 * containment, registration in a session, and eventing are the
 * caller's business (the Bridge's clone-instance verb does all of
 * that); this is just the node operation.
 */
NodeObj CloneObject(NodeObj source)
{
	NodeObj class, inst, interface, prop, valnode, owner;
	msgobj instanceStart;
	char *name, *dir, *val;

	if (!source)
		return NULL;

	class = GetParent(source);
	if (!class)
		return NULL;

	instanceStart = (msgobj) GetPropLong(class, "InstanceStart");
	if (!instanceStart)
		return NULL;

	instanceStart(class, msg_initialize, NULL);
	inst = (NodeObj) GetPropLong(class, "LastInstance");
	if (!inst)
		return NULL;

	interface = GetClassInterface(class);
	for (prop = interface ? GetChild(interface) : NULL; prop; prop = GetNextSibling(prop))
	{
		name = GetPropStr(prop, "Name");
		dir  = GetPropStr(prop, "Direction");
		if (!name || !dir || strcmp(dir, "data") != 0)
			continue;
		if (strcmp(name, "State") == 0)	/* lifecycle, not data */
			continue;

		owner = source;
		valnode = ResolvePort(&owner, name);
		val = valnode ? GetValueStr(valnode) : NULL;
		if (val)
			SetOrDeliverProp(inst, name, val);
	}

	return inst;
}

/* record a subscription on a source port. Each Subscriber carries the   */
/* sink instance, the NAME of the sink port/property the wire lands on,  */
/* and the handler the sink registered as OnMsg there - or 0 for a plain */
/* property, in which case delivery applies the universal default        */
/* (DeliverToSubscriber, node.c: store what arrived). Recording the port */
/* name is what makes the record self-describing: list-connections,     */
/* CloneConnections, Disconnect and the delete scrub all read the wire   */
/* straight off it, no adapter, no reverse handler lookup.               */
void AddSubscription(NodeObj fromPort, NodeObj toNode, char * toPort, long handler){

	NodeObj sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long)toNode);
	if (toPort)
		SetPropStr(sub, "Port", toPort);
	SetPropLong(sub, "Callback", handler);
	AddProp(fromPort, sub);
}

/* see the comment in object.h - a copied group has to arrive wired to  */
/* itself, the same rule a deep-cloned view's aliases already follow    */
void CloneConnections(NodeObj srcInst, NodeObj cloneInst, NodeObj map){

	NodeObj port, sub, sink, sinkClone;
	char *portName, *toPort;

	char dbg[256];

	if (!srcInst || !cloneInst || !map)
		return;

	snprintf(dbg, sizeof(dbg), "CloneConnections: wiring clone of '%s' (its clone is '%s')",
			 GetPropStr(srcInst, "Name"), GetPropStr(cloneInst, "Name"));
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	/* every property is a potential source port - the ones that were    */
	/* actually wired are exactly the ones carrying Subscriber entries    */
	/* (AddSubscription, above), so no separate "is this a port" test is  */
	/* needed or wanted                                                    */
	for (port = GetNextProp(srcInst); port; port = GetNextSibling(port)) {

		portName = GetNameStr(port);
		if (!portName)
			continue;

		for (sub = GetNextProp(port); sub; sub = GetNextSibling(sub)) {

			if (!CmpName(sub, "Subscriber"))
				continue;

			/* every wire is one uniform record naming its REAL sink and    */
			/* the port it lands on ({Instance, Port} - AddSubscription).   */
			/* A record with no Port is not a user wire (a Bridge tap       */
			/* predating a reconnect, a test stub) and is left alone; a     */
			/* sink outside this cloned group (including every tap - taps   */
			/* are never in the map) is left alone too.                     */
			sink = (NodeObj) GetPropLong(sub, "Instance");
			toPort = GetPropStr(sub, "Port");
			if (!sink || !toPort)
				continue;

			sinkClone = (NodeObj) GetConnState(map, (long) sink);

			snprintf(dbg, sizeof(dbg),
					 "CloneConnections:   found wire %s.%s -> '%s'.%s; that sink %s cloned in this group",
					 GetPropStr(srcInst, "Name"), portName, GetPropStr(sink, "Name"), toPort,
					 sinkClone ? "WAS" : "was NOT");
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);

			if (!sinkClone)
				continue;

			/* re-make the wire between the copies with the same call a    */
			/* live one uses - the sink's clone is the same class, so       */
			/* Connect() finds the same handler (or the same absence of     */
			/* one) on the same port name                                    */
			Connect(cloneInst, portName, sinkClone, toPort);

			snprintf(dbg, sizeof(dbg),
					 "CloneConnections:   ADDED wire on clone: '%s'.%s -> '%s'.%s",
					 GetPropStr(cloneInst, "Name"), portName, GetPropStr(sinkClone, "Name"), toPort);
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
		}
	}
}

/*
 * Deep-clone a container and everything in it - the WHOLE clone, in the
 * engine, so it happens the same whoever asked (a script, or the html
 * through the bridge). CloneObject copies one node's data; this copies a
 * group and keeps it self-contained:
 *
 *   - every member (an instance whose Container is srcPath, skipping
 *     hidden plumbing) is cloned and re-homed under clonePath, recursively
 *     into nested containers;
 *   - an alias member is re-pointed at the CLONE of whatever it aliased
 *     inside the group; an alias to something OUTSIDE the group is left
 *     pointing where it did;
 *   - the wires between members are re-made between the clones.
 *
 * Naming the result and telling anyone about it are NOT here - that is the
 * translator's job (the bridge turns objects into html paths and events).
 * Every instance made is recorded in `map` (src pointer -> clone pointer,
 * GetConnState/SetConnState) so the caller can walk the clones out and
 * name/announce them. srcPath/clonePath are the container-path strings the
 * members' Container properties use; the caller owns that convention, the
 * engine only rewrites old -> new. Members keep their own Name (unique
 * already inside a fresh copy of their container); only the top is named
 * by the caller, via clonePath's basename.
 */
/* a thing's session path = its Container plus its Name (root-level things  */
/* have an empty Container and live under /Root). The engine owns this      */
/* now, because the engine owns naming.                                      */
static void InstancePath(NodeObj inst, char *out, int outlen)
{
	char *cont = GetPropStr(inst, "Container");
	char *nm   = GetPropStr(inst, "Name");

	if (cont && cont[0])
		snprintf(out, outlen, "%s/%s", cont, nm ? nm : "");
	else
		snprintf(out, outlen, "/Root/%s", nm ? nm : "");
}

/* is `name` already the Name of some instance sitting in containerPath?    */
/* the engine's own registry walk - names are unique within a container     */
static int NameTakenIn(char *name, char *containerPath)
{
	NodeObj library, class, inst;
	char *cont, *nm;

	for (library = GetChild(RegObjList); library; library = GetNextSibling(library))
		for (class = GetChild(library); class; class = GetNextSibling(class))
			for (inst = GetChild(class); inst; inst = GetNextSibling(inst))
			{
				cont = GetPropStr(inst, "Container");
				if (strcmp(cont ? cont : "", containerPath ? containerPath : "") != 0)
					continue;
				nm = GetPropStr(inst, "Name");
				if (nm && strcmp(nm, name) == 0)
					return 1;
			}
	return 0;
}

/* the engine names a clone: after what the user calls the source, with     */
/* any trailing _N stripped, then the lowest free _k in the target          */
/* container - so a "Slider_1" cloned beside itself becomes Slider_2 (not    */
/* Slider_1_1), and a view "CloneAliasTest" becomes CloneAliasTest_1.        */
static void CloneMintName(NodeObj source, char *containerPath, char *out, int outlen)
{
	char base[200];
	char *nm = GetPropStr(source, "Name");
	char *b  = (nm && nm[0]) ? nm : GetNameStr(GetParent(source));
	int len, k;

	snprintf(base, sizeof(base), "%s", b ? b : "Thing");
	len = (int) strlen(base);
	while (len > 0 && base[len - 1] >= '0' && base[len - 1] <= '9')
		len--;
	if (len > 0 && len < (int) strlen(base) && base[len - 1] == '_')
		base[len - 1] = 0;

	for (k = 1; k < 100000; k++)
	{
		snprintf(out, outlen, "%s_%d", base, k);
		if (!NameTakenIn(out, containerPath))
			return;
	}
}

/* an alias is a link, not a data snapshot - CloneObject can't copy it.  */
/* Make a fresh alias pointing at the clone of what the source aliased    */
/* (map), or at the original if that target was outside the group.        */
static NodeObj CloneAliasNode(NodeObj src, char *container, NodeObj map)
{
	char *propname = GetPropStr(src, "TargetProp");
	NodeObj linknode, targetInst, mapped, inst;
	char *v;
	int i;
	char *carry[] = { "Widget", "Direction", "Label", "X", "Y" };

	if (!propname || !propname[0])
		return NULL;

	linknode = GetPropNode(src, "Value");	/* the alias's doorway slot */
	targetInst = linknode ? (NodeObj) GetPropLong(linknode, "LinkInst") : NULL;
	if (!targetInst)
		return NULL;

	mapped = (NodeObj) GetConnState(map, (long) targetInst);
	if (mapped)
		targetInst = mapped;

	inst = CreateObject(NULL, "Alias");
	if (!inst)
		return NULL;
	if (!LinkPropertyAs(inst, "Value", targetInst, propname))
	{
		DeleteInstance(inst);
		return NULL;
	}

	SetPropStr(inst, "TargetProp", propname);
	for (i = 0; i < (int)(sizeof(carry) / sizeof(carry[0])); i++)
	{
		v = GetPropStr(src, carry[i]);
		if (v && v[0])
			SetPropStr(inst, carry[i], v);
	}
	SetOrDeliverProp(inst, "Container", container ? container : "");
	return inst;
}

/* one pass over a container's direct members (0: clone concrete members,  */
/* 1: clone alias members, 2: clone the wires between them). Later passes  */
/* need every clone to already exist, so it's three sweeps, recursing into */
/* nested views on each. The matching members are snapshotted into `list`  */
/* first, because cloning ADDS instances to the same registry this walks.  */
static void CloneGroupPass(char *srcPath, char *clonePath, NodeObj map, int pass)
{
	NodeObj list, library, class, inst, entry, clone;
	char *cont, *classname, *nm;
	char childSrc[256], childClone[256], key[24];
	char dbg[300];
	int n = 0;

	list = NewNode(INTEGER);
	for (library = GetChild(RegObjList); library; library = GetNextSibling(library))
		for (class = GetChild(library); class; class = GetNextSibling(class))
			for (inst = GetChild(class); inst; inst = GetNextSibling(inst))
			{
				cont = GetPropStr(inst, "Container");
				if (!cont || strcmp(cont, srcPath) != 0)
					continue;
				if (GetPropInt(inst, "_Hidden"))
					continue;	/* plumbing is not content */
				snprintf(key, sizeof(key), "%d", n++);
				SetPropLong(list, key, (long) inst);
			}

	snprintf(dbg, sizeof(dbg), "CLONE pass %d: %d member(s) in '%s' -> '%s'",
			 pass, n, srcPath, clonePath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	for (entry = GetNextProp(list); entry; entry = GetNextSibling(entry))
	{
		inst = (NodeObj) GetValueLong(entry);
		classname = GetNameStr(GetParent(inst));
		nm = GetPropStr(inst, "Name");

		if (strcmp(classname, "Alias") == 0)
		{
			if (pass == 1)
			{
				clone = CloneAliasNode(inst, clonePath, map);
				if (clone)
				{
					if (nm && nm[0])
						SetOrDeliverProp(clone, "Name", nm);
					SetConnState(map, (long) inst, (long) clone);
				}
				snprintf(dbg, sizeof(dbg), "CLONE pass 1: alias member '%s' %s",
						 nm ? nm : "?", clone ? "cloned + linked" : "FAILED");
				DebugPrint(dbg, __FILE__, __LINE__, CLONE);
			}
		}
		else if (pass == 0)
		{
			clone = CloneObject(inst);
			if (clone)
			{
				SetOrDeliverProp(clone, "Container", clonePath);
				if (nm && nm[0])
					SetOrDeliverProp(clone, "Name", nm);
				SetConnState(map, (long) inst, (long) clone);
			}
			snprintf(dbg, sizeof(dbg), "CLONE pass 0: member '%s' (%s) %s",
					 nm ? nm : "?", classname, clone ? "cloned" : "FAILED");
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
		}
		else if (pass == 2)
		{
			clone = (NodeObj) GetConnState(map, (long) inst);
			snprintf(dbg, sizeof(dbg), "CLONE pass 2: member '%s' -> %s",
					 nm ? nm : "?", clone ? "cloning its wires" : "NO CLONE in map (skipped)");
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
			if (clone)
				CloneConnections(inst, clone, map);
		}

		/* a nested view's members live under the nested view's own path */
		if (strcmp(classname, "View") == 0 && nm && nm[0])
		{
			snprintf(childSrc, sizeof(childSrc), "%s/%s", srcPath, nm);
			snprintf(childClone, sizeof(childClone), "%s/%s", clonePath, nm);
			CloneGroupPass(childSrc, childClone, map, pass);
		}
	}

	DelNode(list);
}

NodeObj CloneView(NodeObj source, char *containerPath, NodeObj map)
{
	NodeObj top;
	char name[200], srcPath[256], clonePath[256];
	char dbg[1024];

	if (!source || !map)
		return NULL;

	/* the engine names it (this is the core's job, not the caller's) and  */
	/* works out both paths itself: where the source's members point        */
	/* (srcPath) and where the clone's will (clonePath)                     */
	InstancePath(source, srcPath, sizeof(srcPath));
	CloneMintName(source, containerPath ? containerPath : "", name, sizeof(name));

	snprintf(dbg, sizeof(dbg), "CLONE start: source '%s' -> new name '%s' into container '%s' (source path '%s')",
			 GetPropStr(source, "Name"), name, containerPath ? containerPath : "(root)", srcPath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	top = CloneObject(source);
	if (!top)
		return NULL;
	SetOrDeliverProp(top, "Container", containerPath ? containerPath : "");
	SetOrDeliverProp(top, "Name", name);
	SetConnState(map, (long) source, (long) top);

	if (containerPath && containerPath[0])
		snprintf(clonePath, sizeof(clonePath), "%s/%s", containerPath, name);
	else
		snprintf(clonePath, sizeof(clonePath), "/Root/%s", name);

	/* everything inside it - members, then aliases, then wires */
	CloneGroupPass(srcPath, clonePath, map, 0);
	CloneGroupPass(srcPath, clonePath, map, 1);
	CloneGroupPass(srcPath, clonePath, map, 2);

	snprintf(dbg, sizeof(dbg), "CLONE done: '%s' cloned into '%s'", GetPropStr(source, "Name"), clonePath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	return top;
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	NodeObj fromPort, toPort, fromOwner, toOwner;
	long handler;

	if (!fromNode || !from || !toNode || !to)
		return 0;

	/* both ends resolve through links: wiring to or from an alias IS   */
	/* wiring to or from the original - the Subscriber entry lands on    */
	/* the original's port and carries the original instance, so the     */
	/* alias adds zero cost (and zero code) to every later message       */
	fromOwner = fromNode;
	fromPort = ResolvePort(&fromOwner, from);
	if (!fromPort) {
		/* make sure the output port exists on the source */
		SetPropInt(fromNode, from, 0);
		fromPort = GetPropNode(fromNode, from);
	}

	/* the target property must already exist */
	toOwner = toNode;
	toPort = ResolvePort(&toOwner, to);
	if (!toPort) {
		DebugPrint ( "Connect could not find the input port on the sink.", __FILE__, __LINE__, ERROR);
		return 0;
	}

	/* one record either way - a port with a compiled handler records it,  */
	/* a plain property records Callback 0 and delivery applies the        */
	/* universal default (store what arrived - DeliverToSubscriber,        */
	/* node.c). Every property is both a source and a sink: node.c's       */
	/* unconditional write fan-out is the source half, the default         */
	/* delivery is the sink half, and the Subscriber always names the      */
	/* REAL sink - no adapter standing in between for the graph walkers    */
	/* (list-connections, clone, scrub) to trip over. The recorded name    */
	/* is the RESOLVED node's own - wiring to an alias records what the    */
	/* alias stands for, same rule as SetOrDeliverProp.                    */
	handler = GetPropLong(toPort, "OnMsg");
	AddSubscription(fromPort, toOwner, GetNameStr(toPort), handler);

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "Connect: '%s'.%s -> '%s'.%s (%s)",
				 GetPropStr(fromOwner, "Name"), from, GetPropStr(toOwner, "Name"),
				 GetNameStr(toPort), handler ? "handler" : "default delivery");
		DebugPrint(dbg, __FILE__, __LINE__, WIRE);
	}

	return 1;
}

/* the inverse of Connect() - remove exactly the one wire that matches,   */
/* by the same resolution rules (either end may be an alias). Returns 1   */
/* if a wire was removed, 0 if no such wire existed. Taps and handlers    */
/* are all the same record shape, so "which wire" is just {Instance,      */
/* Port} equality on the resolved sink.                                    */
int
Disconnect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	NodeObj fromPort, toPort, fromOwner, toOwner, sub;
	char * toName;

	if (!fromNode || !from || !toNode || !to)
		return 0;

	fromOwner = fromNode;
	fromPort = ResolvePort(&fromOwner, from);
	if (!fromPort)
		return 0;

	toOwner = toNode;
	toPort = ResolvePort(&toOwner, to);
	if (!toPort)
		return 0;
	toName = GetNameStr(toPort);

	for (sub = GetNextProp(fromPort); sub; sub = GetNextSibling(sub))
	{
		if (!CmpName(sub, "Subscriber"))
			continue;
		if ((NodeObj) GetPropLong(sub, "Instance") != toOwner)
			continue;
		if (!GetPropStr(sub, "Port") || !toName
			|| strcmp(GetPropStr(sub, "Port"), toName) != 0)
			continue;

		RemoveProp(fromPort, sub);
		DelNode(sub);

		{
			char dbg[256];
			snprintf(dbg, sizeof(dbg), "Disconnect: '%s'.%s -/-> '%s'.%s",
					 GetPropStr(fromOwner, "Name"), from, GetPropStr(toOwner, "Name"), toName);
			DebugPrint(dbg, __FILE__, __LINE__, WIRE);
		}

		return 1;
	}

	return 0;
}

/* one of these rides on a queued dispatch task between SndMsg (which   */
/* builds it) and DispatchMsg (which reads it back out when the task    */
/* comes due) - see the comment on SndMsg for why sends are queued.     */
/* Not to be confused with DeliverMsg below, which is a separate,       */
/* synchronous, single-target mechanism (bypasses the subscriber list   */
/* entirely) used by Router and SetOrDeliverProp.                       */
typedef struct {
	NodeObj instance;	/* the source instance that owns outPort - see  */
				/* DeleteInstance's CancelPendingSends, which    */
				/* matches on this to catch a queued send whose  */
				/* source is deleted before delivery fires        */
	NodeObj outPort;
	MsgId   message;
	NodeObj data;
	TaskObj task;
} MsgEnvelope;

/* scheduler callback: actually walk the subscriber list and deliver.   */
/* Runs from ExecTasks, so every hop is a flat call from the scheduler, */
/* never nested inside the sender's own call stack the way a direct    */
/* SndMsg call used to be                                               */
static int
DispatchMsg(NodeObj envArg, NodeObj unused, int reason){

	MsgEnvelope * env = (MsgEnvelope *) envArg;
	NodeObj sub;

	/* outPort is NULL if CancelPendingSends neutralized this envelope   */
	/* because its source instance was deleted before this fired - the   */
	/* port (and everything else the deleted instance owned) is already  */
	/* gone, so there is nothing left to walk, just the payload to free  */
	if (reason == task_callback && env->outPort) {
		sub = GetNextProp(env->outPort);
		while (sub) {
			if (CmpName(sub, "Subscriber"))
				/* one shared definition of delivery (node.c): a       */
				/* recorded handler is called; a plain-property wire   */
				/* gets the universal default - store what arrived     */
				DeliverToSubscriber(sub, env->message, env->data);
			sub = GetNextSibling(sub);
		}
	}

	if (reason == task_callback)
		/* fired normally through ExecTasks, which does not free this  */
		/* task itself - park it for GetTask to hand back out          */
		RemoveTask(env->task);
	/* else task_deactivate: the list was torn down with this message  */
	/* still queued - DeleteTask frees the task_entry itself right     */
	/* after this returns, so it must not go back on the reuse pool    */

	if (env->data)
		DelNode(env->data);

	free(env);

	return rtrn_handled;
}

/* called by DeleteInstance before it frees deadInstance: a message      */
/* deadInstance already sent may still be queued (SndMsg only costs an   */
/* AddTaskNow, delivery happens later), and that queued envelope holds a */
/* raw pointer to one of deadInstance's own ports - about to be freed    */
/* along with the rest of it. Walk every pending DispatchMsg task (both  */
/* the main list and the in-flight runnow bucket - a message can be      */
/* mid-delivery-batch, due this tick but not yet run, when a callback    */
/* earlier in the same batch deletes its source) and blank out outPort   */
/* on any envelope whose source is deadInstance, so DispatchMsg finds    */
/* nothing to walk instead of dereferencing freed memory.                */
static void CancelPendingSends(NodeObj deadInstance)
{
	TaskList tasks = ObjGetTaskList();
	TaskObj task;
	MsgEnvelope * env;

	for (task = GetTaskListHead(tasks); task; task = GetTaskNext(task)) {
		if (GetTaskCallback(task) != (FuncPtr)DispatchMsg)
			continue;
		env = (MsgEnvelope *) GetTaskData(task);
		if (env->instance == deadInstance)
			env->outPort = NULL;
	}

	for (task = GetTaskListHead(GetTaskListRunnow(tasks)); task; task = GetTaskNext(task)) {
		if (GetTaskCallback(task) != (FuncPtr)DispatchMsg)
			continue;
		env = (MsgEnvelope *) GetTaskData(task);
		if (env->instance == deadInstance)
			env->outPort = NULL;
	}
}

/* route one message out a port to every subscriber of that port.       */
/*                                                                       */
/* Delivery is queued through the scheduler rather than called          */
/* synchronously: SndMsg only ever costs one AddTaskNow, and actual     */
/* delivery (walking the subscriber list, calling each handler) happens */
/* later from ExecTasks.  This keeps causality flat - a message and     */
/* everything it causes downstream can never nest inside the call stack */
/* of the object that sent it, so a dense subscriber web (or a filter/  */
/* queue chain that re-sends what it receives) can't starve the         */
/* scheduler by monopolizing the stack; every hop gets its own turn     */
/* through ExecTasks, breadth-first, in the order it was caused.        */
/*                                                                       */
/* SndMsg takes ownership of `data`: DispatchMsg frees it once every     */
/* subscriber has had it, so callers must drop the DelNode they used to  */
/* call right after SndMsg under the old synchronous contract - and      */
/* anything that forwards a message it received (rather than a freshly   */
/* built one) must build its own copy to send onward, since the original */
/* is owned by the sender's own queued delivery, not by whoever received */
/* it (see Filter_OnIn for the pattern).                                 */
int
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data){

	NodeObj outPort, owner;
	MsgEnvelope * env;
	TaskObj task;

	/* sending out an alias port sends out the original - the envelope   */
	/* records the original as its source so CancelPendingSends matches   */
	/* the instance whose port the envelope actually holds                 */
	owner = instance;
	outPort = ResolvePort(&owner, port);
	if (!outPort) {
		if (data)
			DelNode(data);
		return 0;
	}
	instance = owner;

	env = malloc(sizeof(MsgEnvelope));
	env->instance = instance;
	env->outPort = outPort;
	env->message = message;
	env->data    = data;

	task = GetTask(ObjGetTaskList());
	env->task = task;

	/* env rides through the generic NodeObj slot - not a real node,   */
	/* DispatchMsg is the only thing that ever reads it back out       */
	AddTaskNow(task, (FuncPtr)DispatchMsg, message, (NodeObj)env);

	return 1;
}

/*
 * Deliver straight to one named port's own handler, bypassing whatever
 * Subscriber list is (or isn't) attached to it - for an object like
 * Router that decides AT DELIVERY TIME which single target gets a given
 * message, rather than a fixed Connect()'d wire. SndMsg fans out to
 * everyone subscribed to a port; this reaches exactly one target's port
 * directly, the same way SndMsg reaches each subscriber once it's found.
 */
int DeliverMsg(NodeObj target, char *port, MsgId message, NodeObj data){

	NodeObj portNode;
	msgobj handler;

	if (!target || !port)
		return 0;

	portNode = GetPropNode(target, port);
	if (!portNode)
		return 0;

	handler = (msgobj) GetPropLong(portNode, "OnMsg");
	if (!handler)
		return 0;

	handler(target, message, data);
	return 1;
}

/*
 * A property name can resolve to two different things on a target: a
 * plain data property (a direct write is correct - SetProp* fans out to
 * subscribers on its own), or a port carrying an OnMsg
 * handler (Enable, In, ...). A port only actually does anything when a
 * message is delivered to it the way genuine Connect()'d traffic would
 * arrive - writing its raw value with SetPropStr silently changes the
 * text and calls no handler, so Enable, say, would never flip the
 * instance data the object itself is actually gated on. Every place
 * that writes an instance property by name from outside the object
 * (the property-binding adapter below, Bridge's set-property command)
 * needs this distinction, not just SetPropStr.
 */
void SetOrDeliverProp(NodeObj target, char *propname, char *value)
{
	NodeObj propnode, chunk, owner;

	if (!target || !propname || !value)
		return;

	/* writing through an alias writes the original - and the original's */
	/* own fan-out (SetPropStr) or compiled handler (DeliverMsg with the  */
	/* owning instance, so it finds its own "local") does the rest.       */
	/* The write goes by the RESOLVED node's own name, not the name the   */
	/* caller used: an Alias keeps its link in a "Value" slot that may    */
	/* stand for the target's Name, Enable, anything - writing "Value"    */
	/* on the owner would hit the wrong property entirely.                 */
	owner = target;
	propnode = ResolvePort(&owner, propname);
	if (propnode)
		propname = GetNameStr(propnode);
	if (propnode && GetPropLong(propnode, "OnMsg"))
	{
		chunk = NewNode(STRING);
		SetName(chunk, propname);
		SetValueStr(chunk, value);
		DeliverMsg(owner, propname, msg_send, chunk);
		DelNode(chunk);
		return;
	}

	SetPropStr(propnode ? owner : target, propname, value);
}

/* see the comment in object.h - the one shared placement call behind    */
/* create, clone, and move, whatever translator asked                     */
void PlaceInstance(NodeObj inst, char *container, char *x, char *y)
{
	char dbg[1024];

	if (!inst)
		return;

	SetOrDeliverProp(inst, "Container", (container && container[0]) ? container : "");
	if (x && x[0])
		SetOrDeliverProp(inst, "X", x);
	if (y && y[0])
		SetOrDeliverProp(inst, "Y", y);

	snprintf(dbg, sizeof(dbg), "PLACE: '%s' in '%s' at X=%s Y=%s (PanelX=%s PanelY=%s)",
			 GetPropStr(inst, "Name"), GetPropStr(inst, "Container"),
			 GetPropStr(inst, "X"), GetPropStr(inst, "Y"),
			 GetPropStr(inst, "PanelX"), GetPropStr(inst, "PanelY"));
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);
}

/* Would placing the thing whose session path is instPath into container  */
/* put it inside itself? Session paths ARE the containment chain (a       */
/* member's path is its container's path plus its basename - see the      */
/* Bridge's rename machinery), so ancestor-or-self is exactly a prefix    */
/* test. "" is the top-level canvas and can never be inside anything.      */
int ContainmentCycle(char *instPath, char *container)
{
	int len;

	if (!instPath || !instPath[0] || !container || !container[0])
		return 0;

	len = (int) strlen(instPath);
	if (strncmp(container, instPath, len) != 0)
		return 0;

	return container[len] == 0 || container[len] == '/';
}

/* see the comment in object.h - the one-verb move every translator      */
/* shares: validate, then place. Returns 1 moved, 0 refused.              */
int MoveInstance(NodeObj inst, char *instPath, char *container, char *x, char *y)
{
	if (!inst)
		return 0;

	if (ContainmentCycle(instPath, container))
		return 0;

	PlaceInstance(inst, container, x, y);
	return 1;
}

/* see the comment above these in object.h */
long GetConnState(NodeObj table, long connId)
{
	char key[32];

	if (!table)
		return 0;

	snprintf(key, sizeof(key), "%ld", connId);
	return GetPropLong(table, key);
}

void SetConnState(NodeObj table, long connId, long value)
{
	char key[32];

	if (!table)
		return;

	snprintf(key, sizeof(key), "%ld", connId);
	SetPropLong(table, key, value);
}

/*
 * The generic handler behind every instance's Activate port. Modules
 * store their activation function as an "Activate" long property
 * (ActivateInstance calls it); RegisterInstance stamps this handler as
 * OnMsg on that same property node, which makes Activate an ordinary
 * port: Connect(button, "Out", anything, "Activate") is one plain,
 * listable, clonable, scrubbable wire - no separate bind-activate
 * species. It also protects the stored function pointer: without a
 * handler here, a wire into Activate would fall to the default
 * store-what-arrived delivery and overwrite the pointer with a string.
 * msg_eof is ignored, same convention as every Enable handler - an
 * upstream ending is not a click.
 */
int ActivateOnMsg(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;

	ActivateInstance(instance);
	return rtrn_handled;
}

/* LED/TextOut/VUMeter/Label reflect a value; everything else in a       */
/* ControlSpec table (Checkbox/Textbox/Slider/Knob) edits one - see the  */
/* doc comment on BuildSettingsView (object.h) for what each kind wires  */
static int IsDisplayControlClass(char *className)
{
	return strcmp(className, "LED") == 0
		|| strcmp(className, "TextOut") == 0
		|| strcmp(className, "VUMeter") == 0
		|| strcmp(className, "Label") == 0;
}

NodeObj BuildSettingsView(NodeObj target, ControlSpec *specs, int count)
{
	NodeObj control;
	int i;

	if (!target || !specs || count <= 0)
		return NULL;

	for (i = 0; i < count; i++)
	{
		control = CreateObject(NULL, specs[i].controlClass);
		if (!control)
			continue;

		/* position is just a property, same as any other - X/Y/W/H exist  */
		/* on every placeable class (InitPosition) and a plain write is    */
		/* all it takes; no container needs to know about this control     */
		SetPropInt(control, "X", specs[i].x);
		SetPropInt(control, "Y", specs[i].y);
		SetPropInt(control, "W", specs[i].w);
		SetPropInt(control, "H", specs[i].h);

		/* every row is a plain Connect() - a Button reaches the target's   */
		/* Activate port (ActivateOnMsg), a display reflects the property,  */
		/* an input edits it through the universal default delivery         */
		if (strcmp(specs[i].controlClass, "Button") == 0)
			Connect(control, "Out", target, "Activate");
		else if (IsDisplayControlClass(specs[i].controlClass))
			Connect(target, specs[i].property, control, "In");
		else
			Connect(control, "Value", target, specs[i].property);
	}

	return target;
}

/*
 * Retired: SetPropInt/SetPropStr/SetPropLong (node.c) now fan out to a
 * property's Subscriber children unconditionally, on every write, with
 * no opt-in step - a property is watchable simply by existing, exactly
 * like a port already was. WatchableProp used to be how a property got
 * that behavior (installing PropertyChanged as an Intercept); it is kept
 * only so the many existing call sites across the object tree keep
 * compiling. It does nothing now, on purpose - deleting the calls
 * themselves is cleanup, not a fix.
 */
void WatchableProp(NodeObj instance, char *propname)
{
}

/* see the doc comment in object.h - opt-in, ordinary, no different from */
/* any class publishing its own property                                 */
void PublishPosition(NodeObj class)
{
	PublishProp(class, "X", "data", PROP_NULL, "0");
	PublishProp(class, "Y", "data", PROP_NULL, "0");
	PublishProp(class, "W", "data", PROP_NULL, "120");
	PublishProp(class, "H", "data", PROP_NULL, "60");

	/* where something lives is the same kind of fact as where it sits -   */
	/* an ordinary property, not a Slot/membership structure. Empty means   */
	/* "the top-level canvas"; any other value names a View instance's own  */
	/* alias, the same way a wire names an instance - a client renders an   */
	/* instance inside whichever View's Container it currently reads as,    */
	/* correcting late exactly like X/Y already does.                       */
	PublishProp(class, "Container", "data", PROP_NULL, "");

	/* what a thing is CALLED is just one of its properties - writing it   */
	/* renames the instance (the Bridge keys its alias table off it, see   */
	/* Bridge_Set/Bridge_RenameName). Shown as an editable textbox on the  */
	/* dissection table like anything else.                                 */
	PublishProp(class, "Name",   "data", PROP_TEXTBOX, "");

	/* every thing is a view: its icon lives wherever Container says, and  */
	/* its open panel is a peer of every other panel at the root, with a   */
	/* position of its own. Open is only the INITIAL presentation - after  */
	/* first paint, open/closed is each window's own business.              */
	/* PROP_ICON is the engine saying what an alias of Open should look     */
	/* like: another icon for the same thing, a doorway to its one panel -   */
	/* the client renders the stamped Widget instead of special-casing the   */
	/* property name.                                                         */
	PublishProp(class, "Open",   "data", PROP_ICON, "0");
	PublishProp(class, "PanelX", "data", PROP_NULL, "240");
	PublishProp(class, "PanelY", "data", PROP_NULL, "60");

	/* generic, not View-specific - anything CAN be marked undeletable      */
	/* this way, the Palette's own bootstrap instances are just the first    */
	/* thing that actually uses it (BuildPalette). Bridge_Delete is what     */
	/* enforces it.                                                          */
	PublishProp(class, "Deletable", "data", PROP_CHECKBOX, "1");
}

void InitPosition(NodeObj instance)
{
	SetPropInt(instance, "X", 0);
	SetPropInt(instance, "Y", 0);
	SetPropInt(instance, "W", 120);
	SetPropInt(instance, "H", 60);
	SetPropStr(instance, "Container", "");
	SetPropStr(instance, "Deletable", "1");
	SetPropStr(instance, "Name", "");
	SetPropStr(instance, "Open", "0");
	SetPropInt(instance, "PanelX", 240);
	SetPropInt(instance, "PanelY", 60);
}

/* call the Activate function pointer an instance carries on itself */
int
ActivateInstance(NodeObj instance){

	msgobj Activate;

	if (!instance)
		return rtrn_dropped;

	Activate = (msgobj)GetPropLong(instance, "Activate");
	if (!Activate) {
		DebugPrint ( "ActivateInstance found no Activate function on the instance.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	return Activate(instance, msg_initialize, NULL);
}


/* ---- flow scripts: record composition calls, replay them, save/load them ---- */

NodeObj NewFlow(char *name){

	NodeObj flow = NewNode(INTEGER);
	SetName(flow, name);
	return flow;
}

/* every instance a flow creates carries the alias it was created under, */
/* so later Flow* calls on that instance know what to record it as       */
NodeObj FlowCreateObject(NodeObj flow, NodeObj container, char *classname){

	NodeObj inst, instr;
	int count;
	char alias[80];

	inst = CreateObject(container, classname);
	if (!inst)
		return NULL;

	count = GetPropInt(flow, classname) + 1;
	SetPropInt(flow, classname, count);
	snprintf(alias, sizeof(alias), "%s%d", classname, count);

	SetPropStr(inst, "FlowAlias", alias);

	instr = NewNode(INTEGER);
	SetName(instr, "Create");
	SetPropStr(instr, "Class", classname);
	SetPropStr(instr, "As", alias);
	AppendChild(flow, instr);

	return inst;
}

void FlowSetProp(NodeObj flow, NodeObj instance, char *prop, char *value){

	NodeObj instr;
	char *alias;

	if (!instance)
		return;

	SetPropStr(instance, prop, value);

	alias = GetPropStr(instance, "FlowAlias");
	if (!flow || !alias)
		return;

	instr = NewNode(INTEGER);
	SetName(instr, "Set");
	SetPropStr(instr, "Instance", alias);
	SetPropStr(instr, "Prop", prop);
	SetPropStr(instr, "Value", value);
	AppendChild(flow, instr);
}

int FlowConnect(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort){

	NodeObj instr;
	char *fromAlias, *toAlias;
	int ok;

	ok = Connect(fromInst, fromPort, toInst, toPort);
	if (!ok || !flow)
		return ok;

	fromAlias = GetPropStr(fromInst, "FlowAlias");
	toAlias   = GetPropStr(toInst, "FlowAlias");
	if (!fromAlias || !toAlias)
		return ok;

	instr = NewNode(INTEGER);
	SetName(instr, "Connect");
	SetPropStr(instr, "FromInstance", fromAlias);
	SetPropStr(instr, "FromPort", fromPort);
	SetPropStr(instr, "ToInstance", toAlias);
	SetPropStr(instr, "ToPort", toPort);
	AppendChild(flow, instr);

	return ok;
}

int FlowActivateInstance(NodeObj flow, NodeObj instance){

	NodeObj instr;
	char *alias;
	int rc;

	rc = ActivateInstance(instance);

	alias = instance ? GetPropStr(instance, "FlowAlias") : NULL;
	if (flow && alias) {
		instr = NewNode(INTEGER);
		SetName(instr, "Activate");
		SetPropStr(instr, "Instance", alias);
		AppendChild(flow, instr);
	}

	return rc;
}

/* replay a flow script - Create/Set/Connect/Activate instructions, in   */
/* order - into a container, building live instances from scratch. Used */
/* both right after recording (to sanity check it) and after loading a  */
/* script back off disk. The alias table is local to one replay: it     */
/* only has to resolve the instance names the script itself defines.    */
NodeObj RunFlow(NodeObj container, NodeObj flow){

	NodeObj instr, aliases, inst, fromInst, toInst;
	char *classname, *alias, *prop, *value;
	char *fromAlias, *fromPort, *toAlias, *toPort;

	if (!flow)
		return NULL;

	aliases = NewNode(INTEGER);

	instr = GetChild(flow);
	while (instr) {

		if (CmpName(instr, "Create")) {
			classname = GetPropStr(instr, "Class");
			alias     = GetPropStr(instr, "As");
			inst = CreateObject(container, classname);
			if (inst) {
				SetPropStr(inst, "FlowAlias", alias);
				SetPropLong(aliases, alias, (long)inst);
			}
		}
		else if (CmpName(instr, "Set")) {
			alias = GetPropStr(instr, "Instance");
			inst  = (NodeObj) GetPropLong(aliases, alias);
			prop  = GetPropStr(instr, "Prop");
			value = GetPropStr(instr, "Value");
			if (inst)
				SetPropStr(inst, prop, value);
		}
		else if (CmpName(instr, "Connect")) {
			fromAlias = GetPropStr(instr, "FromInstance");
			fromPort  = GetPropStr(instr, "FromPort");
			toAlias   = GetPropStr(instr, "ToInstance");
			toPort    = GetPropStr(instr, "ToPort");
			fromInst  = (NodeObj) GetPropLong(aliases, fromAlias);
			toInst    = (NodeObj) GetPropLong(aliases, toAlias);
			if (fromInst && toInst)
				Connect(fromInst, fromPort, toInst, toPort);
		}
		else if (CmpName(instr, "Activate")) {
			alias = GetPropStr(instr, "Instance");
			inst  = (NodeObj) GetPropLong(aliases, alias);
			if (inst)
				ActivateInstance(inst);
		}

		instr = GetNextSibling(instr);
	}

	DelNode(aliases);
	return flow;
}

int SaveFlow(NodeObj flow, char *filename){

	char *text;
	FILE *f;

	if (!flow || !filename)
		return 0;

	text = NodeToText(flow);
	if (!text)
		return 0;

	f = fopen(filename, "w");
	if (!f) {
		free(text);
		return 0;
	}

	fputs(text, f);
	fclose(f);
	free(text);
	return 1;
}

NodeObj LoadFlow(NodeObj container, char *filename){

	FILE *f;
	long size;
	char *text;
	NodeObj flow;

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	text = malloc(size + 1);
	if (!text) {
		fclose(f);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	flow = TextToNode(text);
	free(text);
	if (!flow)
		return NULL;

	RunFlow(container, flow);
	return flow;
}

/*
 * Needs real registered classes (Pulse, Out), so unlike the other module
 * self-tests it can't run from PerformTesting() inside Init() - that runs
 * before InstallObjects() loads any .object files. Call this after
 * InstallObjects() instead, guarded by the same -t flag.
 */
void FlowTest(NodeObj container){

	NodeObj flow, reloaded, Pulse, Probe;
	char *original, *roundtrip;

	printf("\n\nRunning flow tests\n\n");

	flow = NewFlow("FlowTest");

	Pulse = FlowCreateObject(flow, container, "Pulse");
	Probe = FlowCreateObject(flow, container, "Out");

	if (!Pulse || !Probe) {
		printf("Flow test needs the Pulse and Out classes, skipping.\n");
		return;
	}

	FlowSetProp(flow, Pulse, "Interval", "50");
	FlowSetProp(flow, Pulse, "Count", "1");
	FlowSetProp(flow, Probe, "Label", "flowtest");

	FlowConnect(flow, Pulse, "Out", Probe, "In");

	FlowActivateInstance(flow, Probe);
	FlowActivateInstance(flow, Pulse);

	original = NodeToText(flow);
	printf("Recorded flow: %s\n", original);

	SaveFlow(flow, "flowtest.flow");

	/* replay the saved script into a second, independent pair of instances - */
	/* its probe should print the same messages the original one does, once  */
	/* the scheduler gets to it                                              */
	reloaded = LoadFlow(container, "flowtest.flow");

	roundtrip = NodeToText(reloaded);
	printf("Reloaded script matches the recording: %d\n", strcmp(original, roundtrip) == 0);

	free(original);
	free(roundtrip);
}

static char *KnownClasses[] = {
	"Reader", "Writer", "Pulse", "Filter", "Out", "Queue", "Stack", "TCP", NULL
};

/*
 * Same lifecycle constraint as FlowTest: needs real registered classes,
 * so it has to run after InstallObjects(), not from PerformTesting().
 * This is exactly what a palette would do at startup: ask each known
 * class for its published interface and read off its properties.
 */
void InterfaceTest(){

	int i;
	NodeObj class, interface, prop;
	char *text;

	printf("\n\nRunning interface publication tests\n\n");

	for (i = 0; KnownClasses[i]; i++) {

		class = FindClass(KnownClasses[i]);
		if (!class) {
			printf("%s: class not registered, skipping\n", KnownClasses[i]);
			continue;
		}

		interface = GetClassInterface(class);
		if (!interface) {
			printf("%s: no published interface\n", KnownClasses[i]);
			continue;
		}

		text = NodeToText(interface);
		printf("%s interface: %s\n", KnownClasses[i], text);
		free(text);

		prop = GetChild(interface);
		while (prop) {
			printf("  %-10s %-6s widget=%d default=%s\n",
				GetPropStr(prop, "Name"),
				GetPropStr(prop, "Direction"),
				GetPropInt(prop, "Widget"),
				GetPropStr(prop, "Default"));
			prop = GetNextSibling(prop);
		}
	}
}

/* needs real registered classes, same lifecycle constraint as FlowTest */
/* and InterfaceTest - run after InstallObjects(), not from PerformTesting() */
void SkinTest(){

	NodeObj readerClass, writerClass, skin, custom, layout;
	char *text;
	FILE *f;

	printf("\n\nRunning skin tests\n\n");

	readerClass = FindClass("Reader");
	writerClass = FindClass("Writer");
	if (!readerClass || !writerClass) {
		printf("Skin test needs the Reader and Writer classes, skipping\n");
		return;
	}

	/* nobody has skinned Reader yet - this should generate a default   */
	/* from the interface it already published, one Layout per property */
	skin = GetClassSkin(readerClass);
	text = NodeToText(skin);
	printf("Reader's generated default skin: %s\n", text);
	free(text);

	/* stand in for a hand-edited skin file */
	custom = NewNode(INTEGER);
	SetName(custom, "Skin");
	layout = NewNode(INTEGER);
	SetName(layout, "Layout");
	SetPropStr(layout, "Name", "Filename");
	SetPropStr(layout, "Label", "Input file");
	SetPropInt(layout, "X", 10);
	SetPropInt(layout, "Y", 20);
	SetPropStr(layout, "Style", "highlighted");
	AppendChild(custom, layout);

	text = NodeToText(custom);
	f = fopen("skintest.skin", "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
	free(text);
	DelNode(custom);

	/* loading should replace the generated default outright */
	skin = LoadSkin(readerClass, "skintest.skin");
	text = NodeToText(skin);
	printf("Reader's skin after loading a custom one: %s\n", text);
	free(text);

	printf("GetClassSkin now returns the loaded skin, not a fresh default: %d\n",
		   GetClassSkin(readerClass) == skin);

	/* a class nobody touched still gets its own independent default */
	skin = GetClassSkin(writerClass);
	text = NodeToText(skin);
	printf("Writer (untouched) still generates its own default: %s\n", text);
	free(text);
}

static int PropertyWatchTestMessage;
static int PropertyWatchTestValue;

/* the watcher's In handler - same msgobj shape as any real port handler, */
/* proving a plain property change reaches it exactly like a port would   */
int PropertyWatchTestOnIn(NodeObj instance, MsgId message, NodeObj data)
{
	PropertyWatchTestMessage = message;
	PropertyWatchTestValue = GetValueInt(data);	/* copy now - data is gone once this returns */
	return rtrn_handled;
}

/* pure mechanism, no registered classes needed - two bare instances */
void PropertyWatchTest(){

	NodeObj source, watcher, port;

	printf("\n\nRunning property watch tests\n\n");

	source = NewNode(INTEGER);
	SetName(source, "Source");
	SetPropInt(source, "Level", 0);
	WatchableProp(source, "Level");

	watcher = NewNode(INTEGER);
	SetName(watcher, "Watcher");
	SetPropInt(watcher, "In", 0);
	port = GetPropNode(watcher, "In");
	SetPropLong(port, "OnMsg", (long) PropertyWatchTestOnIn);

	Connect(source, "Level", watcher, "In");

	SetPropInt(source, "Level", 42);

	printf("Watcher saw the property change: message=%d value=%d\n",
		   PropertyWatchTestMessage, PropertyWatchTestValue);
	printf("Property still reads correctly after the watched write: %d\n",
		   GetPropInt(source, "Level"));

	DelNode(source);
	DelNode(watcher);
}


// Handle registration of objects, classes, and instances,
PrintRegInfo(char* message, NodeObj obj){
	char buffer[255];
	sprintf((char *)&buffer, message, GetNameStr(obj));
	DebugPrint ((char *)&buffer, __FILE__, __LINE__, REGISTER);
}

NodeObj RegisterLibrary(NodeObj library){
	PrintRegInfo("Registering object '%s'", library);
	AddChild(RegObjList, library);
	return library;
}

void UnregisterLibrary(NodeObj library){
	PrintRegInfo("Unregistering object '%s'", library);
	SetPropInt(library, "State", 0); 	//Mark this node as gone.

	/* the full node dump is only wanted at high verbose levels */
	if (DebugPrintGetLevel() >= 3)
		PrintNode(library);

	//DelNode(node);  // I stopped removing the node to see it dump out on exit
}

NodeObj RegisterClass(NodeObj library, NodeObj class){
	PrintRegInfo("Registering class '%s'", class);
	AddChild(library, class);
	//PrintNode(library);
	//msgobj InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	//if (InstanceStart) InstanceStart(class, 1, NULL);
	return class;
}

void UnRegisterClass(NodeObj library, NodeObj class){
    PrintRegInfo("Unregistering class '%s'", library);
	//DelNode(node);
}

/* the interface lives as a property on the class, not a child - a       */
/* class's children are its instances (RegisterInstance), and mixing     */
/* interface entries into that list would break anything that walks it   */
NodeObj GetClassInterface(NodeObj class){

	if (!class)
		return NULL;

	return GetPropNode(class, "Interface");
}

/* see the comment in object.h */
NodeObj InterfacePropForInstance(NodeObj inst, char *propname)
{
	NodeObj interface, prop;
	char *name;

	if (!inst || !propname)
		return NULL;

	interface = GetClassInterface(GetParent(inst));
	for (prop = interface ? GetChild(interface) : NULL; prop; prop = GetNextSibling(prop))
	{
		name = GetPropStr(prop, "Name");
		if (name && strcmp(name, propname) == 0)
			return prop;
	}

	return NULL;
}

NodeObj PublishProp(NodeObj class, char *name, char *direction, int widget, char *defaultValue){

	NodeObj interface, entry;

	if (!class || !name)
		return NULL;

	interface = GetClassInterface(class);
	if (!interface) {
		interface = NewNode(INTEGER);
		SetName(interface, "Interface");
		AddProp(class, interface);
	}

	entry = NewNode(INTEGER);
	SetName(entry, "Property");
	SetPropStr(entry, "Name", name);
	SetPropStr(entry, "Direction", direction ? direction : "data");
	SetPropInt(entry, "Widget", widget);
	SetPropStr(entry, "Default", defaultValue ? defaultValue : "");

	AppendChild(interface, entry);

	return entry;
}

#define SKIN_ROW_HEIGHT 30

/* a default layout for a class nobody has skinned yet: one row per      */
/* published property, in the order it was published, stacked vertically */
NodeObj GenerateSkin(NodeObj class){

	NodeObj interface, prop, skin, layout;
	char *name;
	int y;

	skin = NewNode(INTEGER);
	SetName(skin, "Skin");

	interface = GetClassInterface(class);
	prop = interface ? GetChild(interface) : NULL;
	y = 0;

	while (prop) {
		name = GetPropStr(prop, "Name");

		layout = NewNode(INTEGER);
		SetName(layout, "Layout");
		SetPropStr(layout, "Name", name);
		SetPropStr(layout, "Label", name);
		SetPropInt(layout, "X", 0);
		SetPropInt(layout, "Y", y);
		SetPropStr(layout, "Style", "");
		AppendChild(skin, layout);

		y += SKIN_ROW_HEIGHT;
		prop = GetNextSibling(prop);
	}

	return skin;
}

/* the skin lives as a property on the class, same reasoning as the      */
/* interface - it is metadata about the class, not one of its instances  */
NodeObj GetClassSkin(NodeObj class){

	NodeObj skin;

	if (!class)
		return NULL;

	skin = GetPropNode(class, "Skin");
	if (skin)
		return skin;

	skin = GenerateSkin(class);
	AddProp(class, skin);
	return skin;
}

int SaveSkin(NodeObj class, char *filename){

	NodeObj skin;
	char *text;
	FILE *f;

	skin = GetClassSkin(class);
	if (!skin || !filename)
		return 0;

	text = NodeToText(skin);
	if (!text)
		return 0;

	f = fopen(filename, "w");
	if (!f) {
		free(text);
		return 0;
	}

	fputs(text, f);
	fclose(f);
	free(text);
	return 1;
}

/* replaces whatever skin the class currently has (generated default or  */
/* an earlier load) - AddProp shadows it rather than freeing it, same    */
/* leak DelNode already carries everywhere else in this tree (see the    */
/* Phase 8 roadmap note); nothing keeps a reference to the old one       */
NodeObj LoadSkin(NodeObj class, char *filename){

	FILE *f;
	long size;
	char *text;
	NodeObj skin;

	if (!class || !filename)
		return NULL;

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	text = malloc(size + 1);
	if (!text) {
		fclose(f);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	skin = TextToNode(text);
	free(text);
	if (!skin)
		return NULL;

	AddProp(class, skin);
	return skin;
}

NodeObj RegisterInstance(NodeObj class, NodeObj Instance){
	NodeObj activate;

	PrintRegInfo("Registering instance of '%s'", Instance);

	AddChild(class, Instance);

	/* Activate is an ordinary port on every instance that has an          */
	/* activation function: modules store the pointer as an "Activate"     */
	/* property before registering (every InstanceStart ends here), and    */
	/* the generic handler makes it wireable - see ActivateOnMsg. Stamped  */
	/* on the property NODE (never SetProp* by the port's name - the       */
	/* shadowing landmine).                                                 */
	activate = GetPropNode(Instance, "Activate");
	if (activate && !GetPropLong(activate, "OnMsg"))
		SetPropLong(activate, "OnMsg", (long) ActivateOnMsg);

	/* leave the newest instance where CreateObject can find it */
	SetPropLong(class, "LastInstance", (long)Instance);

	return Instance;
}

void
UnRegisterInstance(NodeObj class, NodeObj Instance){
    PrintRegInfo("Unregistering instance of '%s'", class);
	//DelNode(node);
}

/* recursively remove any "Subscriber" property under `node` (properties   */
/* can themselves carry sub-properties, e.g. widget metadata) that targets */
/* deadInstance - see DeleteInstance for why                               */
static void ScrubSubscriberProps(NodeObj node, NodeObj deadInstance)
{
	NodeObj prop, next;

	if (!node)
		return;

	prop = GetNextProp(node);
	while (prop) {
		next = GetNextSibling(prop);

		if (CmpName(prop, "Subscriber") && (NodeObj)GetPropLong(prop, "Instance") == deadInstance) {
			{
				char dbg[256];
				snprintf(dbg, sizeof(dbg), "Scrub: removed a wire into dying '%s' (from port '%s')",
						 GetPropStr(deadInstance, "Name"), GetNameStr(node));
				DebugPrint(dbg, __FILE__, __LINE__, WIRE);
			}
			RemoveProp(node, prop);
			DelNode(prop);
		} else {
			ScrubSubscriberProps(prop, deadInstance);
		}

		prop = next;
	}
}

/* walk every live instance (library -> class -> instance, the same shape */
/* FindClass walks) scrubbing any Subscriber entry that targets           */
/* deadInstance - see DeleteInstance                                       */
static void ScrubRegistrySubscriptions(NodeObj deadInstance)
{
	NodeObj library, class, instance;

	library = GetChild(RegObjList);
	while (library) {
		class = GetChild(library);
		while (class) {
			instance = GetChild(class);
			while (instance) {
				ScrubSubscriberProps(instance, deadInstance);
				instance = GetNextSibling(instance);
			}
			class = GetNextSibling(class);
		}
		library = GetNextSibling(library);
	}
}

/* blank every link (aliased property) under `node`'s props that points   */
/* at deadInstance - the alias survives as a dead control instead of a    */
/* dangling pointer, same policy as scrubbed subscriptions                 */
static void ScrubLinkProps(NodeObj node, NodeObj deadInstance)
{
	NodeObj prop;

	if (!node)
		return;

	for (prop = GetNextProp(node); prop; prop = GetNextSibling(prop))
	{
		if (GetNodeLink(prop) && (NodeObj) GetPropLong(prop, "LinkInst") == deadInstance)
		{
			LinkNode(prop, NULL);
			SetPropLong(prop, "LinkInst", 0);
		}
		ScrubLinkProps(prop, deadInstance);
	}
}

/* walk every live instance (same shape as ScrubRegistrySubscriptions)   */
/* neutralizing links aimed at deadInstance - see DeleteInstance          */
static void ScrubRegistryLinks(NodeObj deadInstance)
{
	NodeObj library, class, instance;

	library = GetChild(RegObjList);
	while (library) {
		class = GetChild(library);
		while (class) {
			instance = GetChild(class);
			while (instance) {
				ScrubLinkProps(instance, deadInstance);
				instance = GetNextSibling(instance);
			}
			class = GetNextSibling(class);
		}
		library = GetNextSibling(library);
	}
}

/* the actual, working removal UnRegisterInstance's own stub never did -  */
/* DelSibling first (unlinks Instance from its class's child chain without */
/* touching the instances after it), then DelNode (frees just this one,   */
/* its own properties and children, now that it's isolated).              */
/* InstanceEnd runs first - every object registers one (RegisterClass),   */
/* but until now nothing ever called it back, so local structs and their  */
/* scheduled tasks outlived the node that owned them: DelNode would free  */
/* the instance out from under a still-armed task, and the next time it   */
/* fired it would hand the callback a dangling NodeObj as its data        */
/*                                                                         */
/* ScrubRegistrySubscriptions and CancelPendingSends close two related    */
/* holes from the same root cause: messages are queued (SndMsg/           */
/* DispatchMsg in this file), not delivered synchronously, so a message   */
/* can still be in flight - already queued but not yet delivered - when   */
/* either end of it is deleted out from under it. DispatchMsg re-reads    */
/* its outPort's live Subscriber list at delivery time rather than a      */
/* frozen snapshot, so stripping every Subscriber entry that points at    */
/* this instance is enough to make an already-queued delivery TO it find  */
/* nothing and safely skip it; CancelPendingSends handles the other       */
/* direction, a message this instance already sent whose envelope still   */
/* points at one of its own (about to be freed) ports. Both are confirmed */
/* against real use-after-frees ASan caught in exactly these scenarios    */
/* before this existed.                                                   */
void DeleteInstance(NodeObj instance)
{
	NodeObj class;
	msgobj instanceEnd;

	if (!instance)
		return;

	/* instances are registered as children of their class (RegisterInstance) */
	class = GetParent(instance);
	if (class) {
		instanceEnd = (msgobj)GetPropLong(class, "InstanceEnd");
		if (instanceEnd)
			instanceEnd(instance, msg_update, NULL);
	}

	ScrubRegistrySubscriptions(instance);
	ScrubRegistryLinks(instance);
	CancelPendingSends(instance);

	DelSibling(instance);
	DelNode(instance);
}
