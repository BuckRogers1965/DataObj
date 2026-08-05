#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "widget.h"
#include "DebugPrint.h"
#include "callback.h"
#include "sched.h"
#include "namespace.h"

/*
 * Addressing (roadmap Phase 1.5): the ENGINE owns path -> instance.
 * Identity was always a path in this system (Container is a path string,
 * "there is no creation path, only a current path") - this is the one
 * resolver over that fact, a character trie (namespace.c, written for
 * exactly this) resolving in O(path length) regardless of session size.
 * Translators (the JSON bridge, script language hosts, the future MCP
 * server) all resolve the same names against the same index instead of
 * each keeping a private alias table; naming something is RegisterPath,
 * un-naming is UnregisterPath (a real delete - retired names reclaim
 * their keys), and the reverse direction needs no table at all:
 * PathOfInstance derives from the Name and Container properties the
 * instance already carries, verified by resolving back.
 */
static NSObj * PathIndex = NULL;

static NSObj * GetPathIndex(void)
{
	if (!PathIndex)
		PathIndex = NSCreate();
	return PathIndex;
}

void RegisterPath(char * path, NodeObj inst)
{
	NodeObj container;
	char    cpath[300], *slash;

	if (!path || !path[0] || !inst)
		return;
	NSInsert(GetPathIndex(), path, (long) inst);

	/* the container just gained a member, and that is otherwise invisible:
	   containment is a property on the CHILD, so its fan-out reaches
	   whoever was already watching that child - nobody, for something that
	   did not exist a moment ago. Recording the new path ON THE CONTAINER
	   makes it an ordinary property write, which fans out to whoever is
	   watching the container. This is the one place every creator meets:
	   import, clone, a bridge create, an object building its own panel.
	   A root has no container to tell. */
	snprintf(cpath, sizeof(cpath), "%s", path);
	slash = strrchr(cpath, '/');
	if (!slash || slash == cpath)
		return;
	*slash = '\0';
	container = ResolvePath(cpath);
	if (container)
		SetPropStr(container, "LastMember", path);
}

void UnregisterPath(char * path)
{
	if (!path || !path[0])
		return;
	NSDelete(GetPathIndex(), path);
}

NodeObj ResolvePath(char * path)
{
	if (!path || !path[0])
		return NULL;
	return (NodeObj) NSSearch(GetPathIndex(), path);
}

/* the derived reverse lookup: an instance's path is its Container plus  */
/* its Name (empty Container means the top-level canvas, /Root). Only a  */
/* path that resolves back to the same instance is returned - anything   */
/* unnamed, engine-internal, or mid-rename simply has no path, the same  */
/* answer a missing alias-table entry used to give.                       */
/* A ROOT IS A VIEW. Nothing about it is special: it is an ordinary View
   instance, made the ordinary way, and everything a session shows lives
   in it exactly as anything lives in any other view. The one and only
   difference is that it has no container - it is the top, so there is
   nothing above it to be in. That is why it is made here rather than
   with CreateObject, which requires a location.

   There can be as many as you like - eventually one per login. Each
   anchors its own namespace, and a Bridge is handed the root of the
   session it serves and walks it like any other view.  */
NodeObj FindClass(char * classname);	/* defined below */

NodeObj CreateRoot(char * name)
{
	NodeObj class, root;
	msgobj InstanceStart;
	char path[160];

	if (!name || !name[0])
		return NULL;

	class = FindClass("View");
	if (!class) {
		DebugPrint("CreateRoot needs the View class", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	if (!InstanceStart)
		return NULL;

	InstanceStart(class, msg_initialize, NULL);
	root = (NodeObj)GetPropLong(class, "LastInstance");
	if (!root)
		return NULL;

	SetPropStr(root, "Name", name);
	SetPropStr(root, "Container", "");	/* the top: nothing above it */
	SetPropStr(root, "ReservedViewOpen", "1");

	snprintf(path, sizeof(path), "/%s", name);
	RegisterPath(path, root);

	return root;
}

int PathOfInstance(NodeObj inst, char * out, int outlen)
{
	char * name, * cont;

	if (!inst || !out || outlen < 2)
		return 0;

	name = GetPropStr(inst, "Name");
	if (!name || !name[0]){
		DebugPrint ( "Instance has no name.", __FILE__, __LINE__, ERROR);
		PrintNode(inst);
		return 0;
	}

	/* Container + name, and nothing invented. Only a ROOT has no
	   container (CreateRoot), and its path is just its own name - every
	   other instance was created somewhere and says so. */
	cont = GetPropStr(inst, "Container");
	if (cont && cont[0])
		snprintf(out, outlen, "%s/%s", cont, name);
	else
		snprintf(out, outlen, "/%s", name);

	return ResolvePath(out) == inst;
}

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
void
loadClasses(void){
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

void
UnloadClasses(void){
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

static NodeObj RootView = NULL;

NodeObj GetRootView(void){ return RootView; }

/* Where BuildSettingsView parks the controls it makes. A control has to be
   created in a container, but the target has no path yet inside its own
   InstanceStart, so the controls live in a named stash view instead - the
   boot phases name it (/Initialize for the palette build, /DefaultApp for the
   default app). Defaults to /Initialize, created on first use. */
static NodeObj SettingsHome = NULL;

void SetSettingsHome(NodeObj view){ SettingsHome = view; }

NodeObj GetPaletteView(void){
	/* resolve, never return the cached pointer: the palette is ordinary
	   content, so a load destroys it and restores a NEW node at the same
	   path. Anything holding the old one is holding freed memory. */
	return ResolvePath("/Root/Palette");
}

/* ************************************************************************
 * DO NOT revert this to `(void) className; return 0;` - it USED TO BE
 * exactly that stub, doing nothing, for a long time, with this same
 * comment already sitting above it describing Bridge/TCP as classes that
 * were SUPPOSED to be excluded. Nobody had actually implemented the body.
 * The visible, reported symptom was "every widget has a tiny panel with
 * no controls" (2026-07-30) - it wasn't every widget, it was every
 * palette-bootstrap instance of a class that was never meant to be
 * independently draggable (MCPAgent, confirmed; Bridge/TCP were always
 * intended too, per the comment below, just never wired up). If you're
 * looking at this function wondering whether the body is safe to delete
 * because it "looks like a one-off special case" - it is not: it is the
 * fix for a real, reported, user-visible bug, and this is the ONE
 * designated hook for it (the call site in BuildPalette already exists
 * and always did). Add MORE excluded classes here as they're found;
 * do not empty this out.
 * ************************************************************************/
/* Bridge and TCP are the transport carrying this very session, not      */
/* something a user composing a dataflow should drag out and rewire -    */
/* the same reason a web page builder doesn't let you drag out "an HTTP  */
/* connection." Not an architectural exclusion - nothing stops either    */
/* from being Connect()ed to or built into a flow file like anything     */
/* else - just a curation choice about what belongs in the palette a     */
/* user actually composes with.                                         */
/* MCPAgent (mcpsource.c) is the SAME kind of case: it's the internal    */
/* shape MCPSource_BuildAgentView constructs one of per discovered tool  */
/* (input/output Textboxes, a Lua Runner, a Submit button, AgentName/    */
/* ConnectorPath wired up) - never something a user creates bare. A      */
/* palette-bootstrap MCPAgent (plain Widget_Create, none of that built)  */
/* is just an empty panel with no controls - useless standalone.         */
static int IsPaletteExcluded(char *className)
{
	return className && (strcmp(className, "Bridge") == 0
						  || strcmp(className, "TCP") == 0
						  || strcmp(className, "MCPAgent") == 0);
}

/* ask an instance for the main view it built for itself: scan its        */
/* properties for one whose value IS a View instance - a widget defined   */
/* one when it built itself, a plain object (TCP, lua, quickjs) has none.  */
/* Nothing is "set" specially; the view a widget created is found by       */
/* scanning. Safe: property values are compared as integers against the    */
/* known View instances, never dereferenced as pointers.                   */
/* ask an instance for its view: a widget took on the View machinery      */
/* (InitPosition/PublishPosition gave it X/Y and the rest of a view's      */
/* presentation), so it has a view. A pure object like TCP never did -     */
/* no X/Y, no view - so it returns NULL and stays out of the palette. No   */
/* list, no foreknowledge; the object itself decides by whether it is a    */
/* view.                                                                    */
NodeObj GetMainView(NodeObj instance)
{
	if (!instance)
		return NULL;
	if (!GetPropNode(instance, "X") || !GetPropNode(instance, "Y"))
		return NULL;
	return instance;
}

void BuildPalette(void){

	NodeObj library, class, inst;
	int slot, pass;
	char alias[128];

	/* no forced Mode, no Deletable protection: the palette is just a     */
	/* View like Root or any panel, and everything in it deletes like     */
	/* anything else - a restart rebuilds it, so nothing here is precious. */
	/* X/Y position the icon; PanelX/PanelY position its open panel        */
	/* (independent - the icon never goes away).                            */
	/* the root view first - everything below is created IN something */
	if (!RootView)
		RootView = CreateRoot("Root");

	PaletteView = Widget_Create(RootView, "View", "Palette");
	SetPropStr(PaletteView, "ReservedViewOpen", "1");	/* views default closed; the palette starts open */
	/* clear of the menus, which sit on the top row of the canvas at y=6 -
	   the palette icon used to land on top of them and swallow every click */
	SetPropInt(PaletteView, "X", 20);
	SetPropInt(PaletteView, "Y", 60);
	SetPropInt(PaletteView, "ReservedViewPanelX", 20);
	SetPropInt(PaletteView, "ReservedViewPanelY", 60);
	/* the palette is not precious in principle - a restart rebuilds it -
	   but losing it mid-session costs you every class you can drag out,
	   so it carries the same ordinary guard the menus do */
	SetPropStr(PaletteView, "Deletable", "0");
	SetPropInt(PaletteView, "W", 290);	/* three 80px columns from x=10, plus slack */
	SetPropInt(PaletteView, "H", 220);	/* the inner area scrolls; resize to taste */

	Palette = NewNode(INTEGER);
	SetName(Palette, "Palette");

	/* every bootstrap instance defaults to X=0,Y=0 (InitPosition) - left    */
	/* alone they'd all stack exactly on top of each other inside the        */
	/* Palette's inner area. A simple three-column grid, just so there is     */
	/* something to look at on first boot - completely ordinary X/Y writes,   */
	/* the user can drag any of them anywhere else afterward like anything    */
	/* else in a View.                                                        */
	slot = 0;

	/* two passes so the simple things come first: a bare control has no
	   panel of its own, a widget does (Widget_Publish stamps it). Sorting
	   here beats sorting by eye every time the palette is opened. */
	for (pass = 0; pass < 2; pass++)
	{
		library = GetChild(RegObjList);
		while (library)
		{
			class = GetChild(library);
			while (class)
			{
				if (GetPropInt(class, "Panel") == pass
					&& !IsPaletteExcluded(GetNameStr(class)))
				{
					/* create + name + register in one call - a palette instance
					   builds its panel (and, e.g., ScriptBox its inner host) in its
					   deferred build, which needs it to resolve by path like any
					   placed object */
					inst = Widget_Create(PaletteView, GetNameStr(class), GetNameStr(class));
					if (inst && GetMainView(inst)) {
						SetPropInt(inst, "X", 10 + (slot % 3) * 80);
						SetPropInt(inst, "Y", 10 + (slot / 3) * 66);
						slot++;

						/* the alias is this instance's full path, same         */
						/* convention as a client-created instance's /Root/...   */
						/* (createInstance, app.js) - see the doc comment above  */
						/* and Bridge_Set's Bridge_Rename for what happens if    */
						/* it ever moves out of here later.                      */
						snprintf(alias, sizeof(alias), "/Root/Palette/%s", GetNameStr(class));
						SetPropStr(Palette, alias, alias);
					}
					else if (inst)
						/* no main view - not a placeable palette item (an atomic
						   control lives INSIDE widgets, never on its own). Undo the
						   create so it is not named/registered into the palette. */
						Widget_Destroy(inst);
				}

				class = GetNextSibling(class);
			}
			library = GetNextSibling(library);
		}
	}
}

static NodeObj Chrome;

/*
 * The app's own topbar chrome (File menu, Mode menu) is not a special
 * client-side concept - it's a small, fixed set of real instances,
 * discovered and addressed exactly the same way the Palette is (one
 * property per well-known name, holding the instance's PATH - never a
 * pointer: a load destroys and rebuilds these like any other content,
 * so anything cached across it would be reading freed nodes). "Eat our own dog
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

	fileMenu = CreateObject(GetRootView(), "MenuButton");
	if (fileMenu) {
		/* an ordinary instance in the root view, with an ordinary name -
		   no short-name category, nothing for a walker to special-case */
		SetPropStr(fileMenu, "Name", "FileMenu");
		RegisterPath("/Root/FileMenu", fileMenu);
		/* somewhere of their own: both menus defaulted to 0,0 and sat
		   exactly on top of each other, so only the top one could ever
		   be clicked or dragged */
		SetPropInt(fileMenu, "X", 8);
		SetPropInt(fileMenu, "Y", 6);
		SetPropStr(fileMenu, "Label", "File");
		SetPropStr(fileMenu, "Items", "Load,Save,Import,Export");
		/* an ordinary property, the same guard the palette carries: you
		   cannot delete the menus you drive the session with. Nothing
		   special about these instances - anything can be marked this way */
		SetPropStr(fileMenu, "Deletable", "0");
		SetPropStr(Chrome, "FileMenu", "/Root/FileMenu");
	}

	modeMenu = CreateObject(GetRootView(), "MenuButton");
	if (modeMenu) {
		SetPropStr(modeMenu, "Name", "ModeMenu");
		RegisterPath("/Root/ModeMenu", modeMenu);
		SetPropInt(modeMenu, "X", 110);
		SetPropInt(modeMenu, "Y", 6);
		SetPropStr(modeMenu, "Label", "Mode");
		SetPropStr(modeMenu, "Items", "Operate,Clone,Alias,Move,Connect,Delete,Options");
		SetPropStr(modeMenu, "Deletable", "0");
		SetPropStr(modeMenu, "Selected", "Operate");
		SetPropStr(Chrome, "ModeMenu", "/Root/ModeMenu");
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

	NodeObj class, inst;
	msgobj InstanceStart;
	char cpath[300];

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

	/* EVERYTHING IS ATTACHED WHERE IT LIVES, AT CREATION - so the place is
	   checked BEFORE anything is made. A missing or unaddressable container
	   is not quietly turned into "the root": nothing is created at all, the
	   reason is printed, and the caller gets NULL. Half-making something and
	   dropping it somewhere convenient helps nobody - it just moves the
	   failure to whoever finds it loose on a canvas later. */
	{
		char dbg[400];

		if (!container) {
			snprintf(dbg, sizeof(dbg),
					 "CreateObject('%s') REFUSED: no container given. Every object "
					 "is created somewhere - pass the view it belongs in.", classname);
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			return NULL;
		}

		if (!PathOfInstance(container, cpath, sizeof(cpath))) {
			snprintf(dbg, sizeof(dbg),
					 "CreateObject('%s') REFUSED: the container has no path of its "
					 "own, so it cannot hold anything yet. container path: '%s'", classname, GetPropStr(container, "Container"));
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			PrintNode(container);
			return NULL;
		}
	}

	/* the class creates and registers the instance itself, */
	/* RegisterInstance leaves it in LastInstance for us     */
	InstanceStart(class, msg_initialize, NULL);

	inst = (NodeObj)GetPropLong(class, "LastInstance");
	if (!inst) {
		DebugPrint("CreateObject: the class made no instance", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	SetPropStr(inst, "Container", cpath);

	{
		char dbg[400];
		snprintf(dbg, sizeof(dbg), "CreateObject %s -> %s", classname, cpath);
		DebugPrint(dbg, __FILE__, __LINE__, PLACE);
	}

	return inst;
}

/*
 * Export a view's subtree to a file the correct way - by SERIALIZING the live
 * node state, not replaying commands. It composes the same objects+wiring any
 * flow does: a Serializer walks `view` (Root = its path) and streams its
 * portable state out Out, a Writer drains that to `path`. The walk is the
 * Serializer's OWN task, so this just wires and activates - the file fills as
 * the flow drains (Serializer -> Writer is Save, the way Reader -> Writer is
 * cat). The plumbing lives off-canvas in a /Export root of its own.
 */
static NodeObj ExportHome = NULL;
static int     exportSeq = 0;

void ExportView(NodeObj view, char *path)
{
	char    viewpath[300], sername[64], wrname[64];
	NodeObj ser, wr;

	if (!view || !path || !path[0] || !PathOfInstance(view, viewpath, sizeof(viewpath)))
		return;

	if (!ExportHome)
		ExportHome = CreateRoot("Export");

	/* fresh, uniquely-named plumbing per export (create + name + register) */
	exportSeq++;
	snprintf(sername, sizeof(sername), "Ser%d", exportSeq);
	snprintf(wrname,  sizeof(wrname),  "Wr%d",  exportSeq);
	ser = Widget_Create(ExportHome, "Serializer", sername);
	wr  = Widget_Create(ExportHome, "Writer",     wrname);
	if (!ser || !wr)
		return;

	SetPropStr(ser, "Root", viewpath);		/* walk this view */
	SetPropStr(wr,  "Filename", path);		/* drain to the file */
	Connect(ser, "Out", wr, "In");
	ActivateInstance(wr);					/* open the file, then */
	ActivateInstance(ser);					/* walk + stream into it */
}

/*
 * Import: the inverse of ExportView - reconstruct a live subtree from
 * what ExportView (the Serializer) wrote. The bridge used to hand-roll
 * this parsing AND the reconstruction itself (Bridge_ImportNode et al,
 * bridge.c), calling back into Bridge_Dispatch for every piece created -
 * work the engine should be doing, reachable with no bridge attached at
 * all, exactly like ExportView already is.
 *
 * Two verbs share this machinery:
 *   ImportView  - a CLONE-DROP: the exported view's own top node is
 *                 re-created (a taken name mints fresh), its children
 *                 keep their recorded names verbatim (a fresh container
 *                 can't collide with itself).
 *   LoadView    - RESTORE IN PLACE: `container`'s CURRENT children are
 *                 destroyed first, then the file's own top-level node is
 *                 NOT re-created (container already exists in its place,
 *                 same as ExportView(root,...) exported it) - its
 *                 children are imported straight into container,
 *                 verbatim (container was just cleared, nothing collides).
 */

/* ---- a parser for the Serializer's own {class,name,props,wires,
   children} shape - NOT the shape TextToNode/NodeToText use (that's a
   node's own type/value; this is a class + published-state snapshot) ---- */

static void IJ_Ws(char **pp)
{
	char *p = *pp;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	*pp = p;
}

/* parse a JSON string token at *pp (must be on the opening quote); returns
   the malloc'd, unescaped contents and advances *pp past the closing quote. */
static char *IJ_Str(char **pp)
{
	char *p = *pp, *out, *o;

	if (*p != '"')
		return NULL;
	p++;
	out = malloc(strlen(p) + 1);
	o = out;
	while (*p && *p != '"')
	{
		if (*p == '\\')
		{
			p++;
			switch (*p)
			{
				case 'n': *o++ = '\n'; break;
				case 't': *o++ = '\t'; break;
				case 'r': *o++ = '\r'; break;
				case 'b': *o++ = '\b'; break;
				case 'f': *o++ = '\f'; break;
				case '/': *o++ = '/';  break;
				case '"': *o++ = '"';  break;
				case '\\': *o++ = '\\'; break;
				case 'u':
				{
					int h = 0, i;
					p++;
					for (i = 0; i < 4 && *p; i++)
					{
						char c = *p;
						h <<= 4;
						if (c >= '0' && c <= '9') h |= c - '0';
						else if (c >= 'a' && c <= 'f') h |= c - 'a' + 10;
						else if (c >= 'A' && c <= 'F') h |= c - 'A' + 10;
						p++;
					}
					p--;				/* the loop ++ below re-consumes one */
					if (h < 0x80)
						*o++ = (char) h;
					else if (h < 0x800)
					{
						*o++ = 0xC0 | (h >> 6);
						*o++ = 0x80 | (h & 0x3F);
					}
					else
					{
						*o++ = 0xE0 | (h >> 12);
						*o++ = 0x80 | ((h >> 6) & 0x3F);
						*o++ = 0x80 | (h & 0x3F);
					}
					break;
				}
				default: *o++ = *p; break;
			}
			if (*p)
				p++;
		}
		else
			*o++ = *p++;
	}
	if (*p != '"')
	{
		free(out);
		return NULL;
	}
	p++;
	*o = '\0';
	*pp = p;
	return out;
}

/* an unused path like <prefix>/<Base>_N - server-generated, since a deep
   clone or an import names things no caller asked for individually */
static void ImportFreshName(char *prefix, char *base, char *out, int outlen)
{
	int n;

	for (n = 1; n < 100000; n++)
	{
		snprintf(out, outlen, "%s/%s_%d", (prefix && prefix[0]) ? prefix : "/Root", base, n);
		if (!ResolvePath(out))
			return;
	}
}

/* create one instance the way a live create-instance would (naming,
   placement, data properties) - direct engine calls, no bridge command
   round trip. Returns its actual minted full path (caller frees), NULL
   on failure. force=1: caller guarantees the name is free and it MUST be
   kept verbatim (an internal node of a fresh container, or LoadView's
   own just-cleared container - either way nothing can collide). */
static char *ImportCreate(char *className, char *nodeName,
						   NodeObj propbag, char *containerPath, int force)
{
	NodeObj home, inst, p;
	char    desired[320], fresh[320], *x, *y, *ident, *alias, *slash, dbg[512];
	char   *cpath = (containerPath && containerPath[0]) ? containerPath : "/Root";

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE enter: class='%s' name='%s' container='%s' force=%d",
			 className ? className : "(null)", nodeName ? nodeName : "(null)", cpath, force);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	if (!className || !className[0])
		return NULL;

	/* the node's IDENTITY is its "Name" PROP (Slider_1), NOT the JSON
	   "name" field (the class node's own name, "Slider", same for every
	   instance) - relative links are stored by the Name prop
	   (PathOfInstance uses it), so import must recreate each node under
	   that same name or every link misses. */
	ident = propbag ? GetPropStr(propbag, "Name") : NULL;
	if (!ident || !ident[0])
		ident = nodeName;

	home = ResolvePath(cpath);
	if (!home)
	{
		DebugPrint("IMPORT-CREATE: container path did not resolve, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	inst = CreateObject(home, className);
	if (!inst)
	{
		DebugPrint("IMPORT-CREATE: CreateObject failed (unknown class?), bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE: instance created at %p, placing", (void *) inst);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	x = propbag ? GetPropStr(propbag, "X") : NULL;
	y = propbag ? GetPropStr(propbag, "Y") : NULL;
	PlaceInstance(inst, cpath, (x && x[0]) ? x : "0", (y && y[0]) ? y : "0");

	alias = NULL;
	if (ident && ident[0])
	{
		snprintf(desired, sizeof(desired), "%s/%s", cpath, ident);
		alias = desired;
	}
	if (!alias || !alias[0] || (!force && ResolvePath(alias)))
	{
		/* the name is KNOWN - a view called Connect stays called Connect.
		   Only uniqueness is in question here, so suffix the name it has
		   (Connect_1) rather than mint from the class and lose it (View_1). */
		ImportFreshName(cpath, (ident && ident[0]) ? ident : className,
						fresh, sizeof(fresh));
		alias = fresh;
	}

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE: registering path '%s'", alias);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	RegisterPath(alias, inst);
	slash = strrchr(alias, '/');
	SetOrDeliverProp(inst, "Name", slash ? slash + 1 : alias);

	/* the rest of the saved properties - skip identity/geometry (set
	   above) and State (a runtime readout, not portable state).

	   SetPropStr, never SetOrDeliverProp: restoring saved state is not the
	   same act as sending a message. SetPropStr updates the value in place
	   and fans out to whatever subscribed to it - it changed, subscribers
	   are told - without invoking the property's OWN handler.
	   SetOrDeliverProp does invoke it, and that is what made restore
	   destroy the values it was restoring: writing a saved MenuButton "In"
	   ran MenuButton_OnIn, which mirrors its input into "Selected",
	   overwriting the real saved Selected with whatever stale thing In
	   happened to hold ("0"). Delivering was the error - nothing about the
	   property's name, and no annotation on it would have prevented it. */
	if (propbag)
		for (p = GetNextProp(propbag); p; p = GetNextSibling(p))
		{
			char *pn = GetNameStr(p);

			if (!pn || !strcmp(pn, "X") || !strcmp(pn, "Y") || !strcmp(pn, "Name")
				|| !strcmp(pn, "Container") || !strcmp(pn, "State"))
				continue;
			SetPropStr(inst, pn, GetValueStr(p));
		}

	snprintf(dbg, sizeof(dbg), "IMPORT-CREATE done: '%s' -> path '%s'", className, alias);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	return strdup(alias);
}

/* an Alias is a LINK, not a data snapshot - creating it as a plain
   instance and copying its Target string leaves a dead control pointing
   at the original. So aliases are not created in the build pass; they
   are remembered here and remade afterwards (ImportAliasesPass), once
   every target exists, via a real LinkPropertyAs onto the resolved
   target. `containerPath` is the parent's ACTUAL new path. */
static void ImportDeferAlias(NodeObj propbag, char *containerPath, NodeObj deferred)
{
	NodeObj c = NewNode(INTEGER);
	char   *v;

	SetName(c, "alias");
	v = propbag ? GetPropStr(propbag, "Target") : NULL;
	SetPropStr(c, "of_old", v ? v : "");
	v = propbag ? GetPropStr(propbag, "TargetProp") : NULL;
	SetPropStr(c, "prop", v ? v : "");
	SetPropStr(c, "container", (containerPath && containerPath[0]) ? containerPath : "/Root");
	v = propbag ? GetPropStr(propbag, "X") : NULL;
	SetPropStr(c, "x", v ? v : "0");
	v = propbag ? GetPropStr(propbag, "Y") : NULL;
	SetPropStr(c, "y", v ? v : "0");
	v = propbag ? GetPropStr(propbag, "Widget") : NULL;
	SetPropStr(c, "Widget", v ? v : "");
	v = propbag ? GetPropStr(propbag, "Label") : NULL;
	SetPropStr(c, "Label", v ? v : "");
	AppendChild(deferred, c);
}

/* create a node if it is a concrete instance, or defer it if it is an
   Alias. Returns the new path for concrete nodes (so children parent
   onto it), "" for aliases (they hold no children), NULL on failure. */
static char *ImportPlace(char *className, char *nodeName, NodeObj propbag,
						  char *containerPath, NodeObj deferred, int force)
{
	if (className && strcmp(className, "Alias") == 0)
	{
		ImportDeferAlias(propbag, containerPath, deferred);
		return strdup("");
	}
	return ImportCreate(className, nodeName, propbag, containerPath, force);
}

/* parse one {class,name,props,wires,children} object at *pp and recreate
   it (and, recursively, its children) under containerPath. Returns the
   node's actual minted path (caller frees), "" for an alias, NULL on a
   malformed object.

   skipSelf: parse this node's own fields (advancing the cursor, and
   validating the file) but do NOT create it - its "children" (and any
   of its own "wires", though a container itself rarely has any) are
   processed with containerPath as their own container directly. This is
   LoadView's own top-level node: container already exists in its place
   (ExportView(root,...) is what wrote this entry), so nothing about it
   is re-created - only its contents are (re)built. */
static char *ImportNode(char **pp, char *containerPath, NodeObj deferred,
						 NodeObj wires, int isTop, int skipSelf)
{
	char   *key, *className = NULL, *nodeName = NULL, *actualPath = NULL;
	NodeObj propbag = NULL;
	int     force = !isTop;		/* only a fresh drop's own top re-mints */
	char    dbg[400];

	snprintf(dbg, sizeof(dbg), "IMPORT-NODE enter: container='%s' isTop=%d skipSelf=%d force=%d",
			 containerPath ? containerPath : "(null)", isTop, skipSelf, force);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	IJ_Ws(pp);
	if (**pp != '{')
	{
		DebugPrint("IMPORT-NODE: expected '{', malformed input, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	(*pp)++;

	for (;;)
	{
		IJ_Ws(pp);
		if (**pp == '}')
		{
			(*pp)++;
			break;
		}
		key = IJ_Str(pp);
		if (!key)
			goto fail;
		IJ_Ws(pp);
		if (**pp != ':')
		{
			free(key);
			goto fail;
		}
		(*pp)++;
		IJ_Ws(pp);

		if (strcmp(key, "class") == 0)
			className = IJ_Str(pp);
		else if (strcmp(key, "name") == 0)
			nodeName = IJ_Str(pp);
		else if (strcmp(key, "props") == 0)
		{
			if (**pp != '{')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			propbag = NewNode(INTEGER);
			for (;;)
			{
				char *pk, *pv;

				IJ_Ws(pp);
				if (**pp == '}')
				{
					(*pp)++;
					break;
				}
				pk = IJ_Str(pp);
				if (!pk)
				{
					free(key);
					goto fail;
				}
				IJ_Ws(pp);
				if (**pp != ':')
				{
					free(pk);
					free(key);
					goto fail;
				}
				(*pp)++;
				IJ_Ws(pp);
				pv = IJ_Str(pp);
				if (!pv)
				{
					free(pk);
					free(key);
					goto fail;
				}
				SetPropStr(propbag, pk, pv);
				free(pk);
				free(pv);
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else if (strcmp(key, "wires") == 0)
		{
			/* this node's outgoing connections. It must exist to be the
			   wire's `from`; the sink `to` is an original path, remapped
			   in the wire pass once every instance exists. */
			if (!skipSelf && !actualPath)
			{
				actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE (wires-branch) placed -> %s",
						 actualPath ? actualPath : "(null/failed)");
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
			}
			IJ_Ws(pp);
			if (**pp != '[')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			for (;;)
			{
				char *wf = NULL, *wt = NULL, *wp = NULL, *wk;

				IJ_Ws(pp);
				if (**pp == ']')
				{
					(*pp)++;
					break;
				}
				if (**pp != '{')
				{
					free(key);
					goto fail;
				}
				(*pp)++;
				for (;;)
				{
					char *wv;

					IJ_Ws(pp);
					if (**pp == '}')
					{
						(*pp)++;
						break;
					}
					wk = IJ_Str(pp);
					if (!wk)
					{
						free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					IJ_Ws(pp);
					if (**pp != ':')
					{
						free(wk); free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					(*pp)++;
					IJ_Ws(pp);
					wv = IJ_Str(pp);
					if (!wv)
					{
						free(wk); free(wf); free(wt); free(wp); free(key);
						goto fail;
					}
					if (!strcmp(wk, "from"))      { free(wf); wf = wv; }
					else if (!strcmp(wk, "to"))   { free(wt); wt = wv; }
					else if (!strcmp(wk, "port")) { free(wp); wp = wv; }
					else free(wv);
					free(wk);
					IJ_Ws(pp);
					if (**pp == ',')
						(*pp)++;
				}
				/* record from OUR new path -> the sink's ORIGINAL path
				   (resolved once every instance exists, ImportWiresPass) -
				   an alias/skipped node carries no path, wires nothing */
				if (actualPath && actualPath[0] && wf && wt)
				{
					NodeObj w = NewNode(INTEGER);
					SetPropStr(w, "from", actualPath);
					SetPropStr(w, "fromPort", wf);
					SetPropStr(w, "to_old", wt);
					SetPropStr(w, "toPort", wp ? wp : "");
					AppendChild(wires, w);
				}
				free(wf); free(wt); free(wp);
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else if (strcmp(key, "children") == 0)
		{
			/* the parent must exist before its children can name it as
			   their container - class/name/props are all in by now */
			if (!skipSelf && !actualPath)
			{
				actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE (children-branch) placed -> %s",
						 actualPath ? actualPath : "(null/failed)");
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
			}
			IJ_Ws(pp);
			if (**pp != '[')
			{
				free(key);
				goto fail;
			}
			(*pp)++;
			for (;;)
			{
				char *cp;

				IJ_Ws(pp);
				if (**pp == ']')
				{
					(*pp)++;
					break;
				}
				/* children: verbatim, own X/Y (isTop=0, force=1) - a
				   skipped top's own children are container's TOP-LEVEL
				   entries instead, and ALSO force=1: container was just
				   destroyed (LoadView), so every recorded name is
				   guaranteed free - use it exactly, never mint an
				   approximation */
				snprintf(dbg, sizeof(dbg), "IMPORT-NODE recursing into child under '%s'",
						 skipSelf ? containerPath : (actualPath ? actualPath : containerPath));
				DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
				cp = ImportNode(pp, skipSelf ? containerPath : (actualPath ? actualPath : containerPath),
								deferred, wires, 0, 0);
				if (cp)
					free(cp);
				else
				{
					DebugPrint("IMPORT-NODE: child failed, bail", __FILE__, __LINE__, IMPORT);
					goto childfail;
				}
				IJ_Ws(pp);
				if (**pp == ',')
					(*pp)++;
			}
		}
		else
		{
			char *sk = IJ_Str(pp);		/* unknown key: skip its string value */
			if (sk)
				free(sk);
		}
		free(key);
		IJ_Ws(pp);
		if (**pp == ',')
			(*pp)++;
	}

	/* a childless, wireless node was never created above - do it now */
	if (!skipSelf && !actualPath)
	{
		actualPath = ImportPlace(className, nodeName, propbag, containerPath, deferred, force);
		snprintf(dbg, sizeof(dbg), "IMPORT-NODE (fallback) placed -> %s",
				 actualPath ? actualPath : "(null/failed)");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	}

	snprintf(dbg, sizeof(dbg), "IMPORT-NODE exit OK: -> '%s'", actualPath ? actualPath : "");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (className) free(className);
	if (nodeName)  free(nodeName);
	if (propbag)   DelNode(propbag);
	return actualPath ? actualPath : strdup("");

childfail:
	free(key);
fail:
	DebugPrint("IMPORT-NODE exit FAIL: malformed input", __FILE__, __LINE__, IMPORT);
	if (className) free(className);
	if (nodeName)  free(nodeName);
	if (propbag)   DelNode(propbag);
	if (actualPath) free(actualPath);
	return NULL;
}

/* resolve a saved link target against the imported root. A RELATIVE
   target (no leading '/') pointed inside the exported subtree - prepend
   `importRoot` (the container everything was imported under), and
   because a fresh container keeps its children's names it lands on the
   copy. An ABSOLUTE target pointed OUTSIDE the exported subtree - leave
   it, it still names the live original. An EMPTY-BUT-PRESENT target is
   RelTo's own "the root itself" convention (serializer.c: a wire or
   link pointing at the exported subtree's own top node collapses to ""
   rather than a relative path, since there's nothing to strip a prefix
   off of) - that resolves to importRoot itself, NOT "no target": a
   member wired back to its own container (a composite widget's inner
   logic Connect()'d to the container's own port, e.g.) was silently
   never reconnected on import before this, with no error - the wire
   just quietly didn't exist. A genuinely absent field (saved == NULL,
   never written at all) is still "no target". Writes into `out`,
   returns it. */
static char *ImportResolveTarget(char *importRoot, char *saved, char *out, int len)
{
	if (!saved)
	{
		out[0] = '\0';
		return out;
	}
	if (!saved[0])
	{
		snprintf(out, len, "%s", (importRoot && importRoot[0]) ? importRoot : "/Root");
		return out;
	}
	if (saved[0] == '/')
		snprintf(out, len, "%s", saved);
	else
		snprintf(out, len, "%s/%s", (importRoot && importRoot[0]) ? importRoot : "/Root", saved);
	return out;
}

/* second pass: every alias remembered during the build, now remade as a
   real link (direct engine calls: CreateObject + LinkPropertyAs, not a
   bridge command round trip). */
static void ImportAliasesPass(char *importRoot, NodeObj deferred)
{
	NodeObj d;
	char    dbg[512];

	snprintf(dbg, sizeof(dbg), "IMPORT-ALIASES-PASS enter: importRoot='%s'", importRoot ? importRoot : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	for (d = GetChild(deferred); d; d = GetNextSibling(d))
	{
		char    of[320], fresh[320];
		char   *container, *prop, *w, *lb, *alias, *slash;
		NodeObj target, home, inst, owner, node, pub;

		ImportResolveTarget(importRoot, GetPropStr(d, "of_old"), of, sizeof(of));
		target = of[0] ? ResolvePath(of) : NULL;
		prop = GetPropStr(d, "prop");
		container = GetPropStr(d, "container");
		snprintf(dbg, sizeof(dbg), "IMPORT-ALIASES-PASS: of='%s' target=%p prop='%s' container='%s'",
				 of, (void *) target, prop ? prop : "(null)", container ? container : "(null)");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		if (!target || !prop || !prop[0] || !container)
		{
			DebugPrint("IMPORT-ALIASES-PASS: unresolved target/prop/container, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}

		home = ResolvePath(container);
		if (!home)
		{
			DebugPrint("IMPORT-ALIASES-PASS: container path did not resolve, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}
		inst = CreateObject(home, "Alias");
		if (!inst)
		{
			DebugPrint("IMPORT-ALIASES-PASS: CreateObject(Alias) failed, skip", __FILE__, __LINE__, IMPORT);
			continue;
		}

		if (!LinkPropertyAs(inst, "Value", target, prop))
		{
			DeleteInstance(inst);
			continue;
		}

		/* record the FINAL original, not whatever happened to be linked -
		   aliasing an alias collapses to the original at the link level */
		owner = target;
		node = ResolvePort(&owner, prop);
		if (node)
			prop = GetNameStr(node);
		if (owner != target && PathOfInstance(owner, of, sizeof(of)))
			target = owner;

		pub = InterfacePropForInstance(owner, prop);
		if (pub)
			SetPropInt(inst, "Widget", GetPropInt(pub, "Widget"));

		SetPropStr(inst, "Target", of);
		SetPropStr(inst, "TargetProp", prop);

		PlaceInstance(inst, container, GetPropStr(d, "x"), GetPropStr(d, "y"));

		ImportFreshName(container, "Alias", fresh, sizeof(fresh));
		alias = fresh;
		RegisterPath(alias, inst);
		slash = strrchr(alias, '/');
		SetOrDeliverProp(inst, "Name", slash ? slash + 1 : alias);

		/* restore the alias's own look (create-alias stamps the target's
		   published default; the saved alias may have been restyled) */
		w  = GetPropStr(d, "Widget");
		lb = GetPropStr(d, "Label");
		if (w && w[0])
			SetOrDeliverProp(inst, "Widget", w);
		if (lb && lb[0])
			SetOrDeliverProp(inst, "Label", lb);

		/* three paths into one line: bound each so the whole always fits */
		snprintf(dbg, sizeof(dbg),
				 "IMPORT-ALIASES-PASS: alias '%.140s' -> ('%.140s','%.140s') done",
				 alias, of, prop);
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	}
	DebugPrint("IMPORT-ALIASES-PASS done", __FILE__, __LINE__, IMPORT);
}

/* third pass: the wires. from is already OUR minted path; to is the saved
   sink (relative inside the import -> resolved under importRoot, absolute
   outside -> left alone), connected directly (Connect(), not a bridge
   command) once every instance and alias exists. */
static void ImportWiresPass(char *importRoot, NodeObj wires)
{
	NodeObj w;
	char    dbg[512];

	snprintf(dbg, sizeof(dbg), "IMPORT-WIRES-PASS enter: importRoot='%s'", importRoot ? importRoot : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	for (w = GetChild(wires); w; w = GetNextSibling(w))
	{
		char    to[320];
		char   *from = GetPropStr(w, "from");
		NodeObj fromInst, toInst;

		ImportResolveTarget(importRoot, GetPropStr(w, "to_old"), to, sizeof(to));
		fromInst = (from && from[0]) ? ResolvePath(from) : NULL;
		toInst = to[0] ? ResolvePath(to) : NULL;
		snprintf(dbg, sizeof(dbg), "IMPORT-WIRES-PASS: from='%s'(%p) to='%s'(%p) port='%s'/'%s'",
				 from ? from : "(null)", (void *) fromInst, to, (void *) toInst,
				 GetPropStr(w, "fromPort") ? GetPropStr(w, "fromPort") : "", GetPropStr(w, "toPort") ? GetPropStr(w, "toPort") : "");
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		if (fromInst && toInst)
			Connect(fromInst, GetPropStr(w, "fromPort"), toInst, GetPropStr(w, "toPort"));
		else
			DebugPrint("IMPORT-WIRES-PASS: endpoint missing, skipped", __FILE__, __LINE__, IMPORT);
	}
	DebugPrint("IMPORT-WIRES-PASS done", __FILE__, __LINE__, IMPORT);
}

/*
 * The "destroy" half of LoadView's restore-in-place, staggered through
 * the scheduler exactly the way the cat flow's Reader parses a file -
 * one unit of work per re-armed task, emitted as its own top-level
 * ExecTasks call, never a native C loop calling DeleteInstance (and
 * everything DeleteInstance itself fans out - ScrubRegistrySubscriptions,
 * CancelPendingSends, the removal SndMsg a bridge sends per victim)
 * hundreds of times back to back inside one call stack. That tight-loop
 * shape is exactly what "queued through the scheduler... never nests
 * inside the sender's call stack" (SndMsg's own doc comment) exists to
 * prevent, and a full-session load is the first thing in this codebase
 * large enough (hundreds of instances) to actually hit the case: a
 * synchronous burst that size corrupted scheduler task-pool state and
 * crashed AddTaskDelay a few hundred calls later (confirmed from a core
 * dump: task->owner read back as a stomped 0x559a00000001, a classic
 * heap-corruption signature, not a null or a logic bug tied to any one
 * instance). VNOS's own file object is the reference for this shape:
 * parse a chunk, emit a message, let the scheduler bring you back for
 * the next chunk - never the whole file in one call.
 *
 * The snapshot walk itself (registry-wide, but read-only - no
 * DeleteInstance, no SndMsg) stays a single synchronous pass; only the
 * deletions are staggered, one per task.
 */
typedef struct
{
	NodeObj  victims;			/* scratch: children are path -> long(NodeObj) */
	NodeObj  cursor;			/* next victim to process */
	NodeObj  container;
	TaskObj  task;
	void   (*onDone)(NodeObj container, void *ctx);
	void    *ctx;
} DestroyCtx;

static int DestroyContentsStep(NodeObj arg, NodeObj unused, int reason)
{
	DestroyCtx *dc = (DestroyCtx *) arg;
	NodeObj     entry, victim;
	char        dbg[400];

	(void) unused;

	if (reason != task_callback)
	{
		DebugPrint("DESTROY-CONTENTS-STEP: deactivated mid-batch, cleaning up without finishing", __FILE__, __LINE__, IMPORT);
		DelNode(dc->victims);
		free(dc);
		return rtrn_handled;
	}

	entry = dc->cursor;
	if (!entry)
	{
		NodeObj container = dc->container;
		void  (*onDone)(NodeObj, void *) = dc->onDone;
		void   *ctx = dc->ctx;

		DebugPrint("DESTROY-CONTENTS-STEP: batch done, calling onDone", __FILE__, __LINE__, IMPORT);
		DelNode(dc->victims);
		RemoveTask(dc->task);
		free(dc);
		if (onDone)
			onDone(container, ctx);
		return rtrn_handled;
	}

	dc->cursor = GetNextSibling(entry);
	victim = (NodeObj) GetValueLong(entry);
	if (victim)
	{
		snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-STEP: deleting '%s' (%p)", GetNameStr(entry), (void *) victim);
		DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
		UnregisterPath(GetNameStr(entry));
		DeleteInstance(victim);
	}

	/* re-arm the SAME task for the next victim - a fresh top-level      */
	/* ExecTasks entry, never nested inside this call                     */
	AddTaskNow(dc->task, (FuncPtr) DestroyContentsStep, 0, (NodeObj) dc);
	return rtrn_handled;
}

static void DestroyContentsAsync(NodeObj container, void (*onDone)(NodeObj container, void *ctx), void *ctx)
{
	char       ownPath[300], prefix[320], pbuf[300], dbg[512];
	int        preLen, n = 0;
	NodeObj    lib, cls, mem, snap;
	DestroyCtx *dc;

	DebugPrint("DESTROY-CONTENTS-ASYNC enter", __FILE__, __LINE__, IMPORT);

	if (!container || !PathOfInstance(container, ownPath, sizeof(ownPath)))
	{
		DebugPrint("DESTROY-CONTENTS-ASYNC: container missing/unpathable, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, ctx);
		return;
	}
	snprintf(prefix, sizeof(prefix), "%s/", ownPath);
	preLen = (int) strlen(prefix);
	snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-ASYNC: scanning under '%s'", prefix);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	snap = NewNode(INTEGER);
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
	 for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
	  for (mem = GetChild(cls); mem; mem = GetNextSibling(mem))
	  {
		if (!PathOfInstance(mem, pbuf, sizeof(pbuf)))
			continue;
		if (strncmp(pbuf, prefix, preLen) != 0)
			continue;
		SetPropLong(snap, pbuf, (long) mem);
		n++;
	  }
	snprintf(dbg, sizeof(dbg), "DESTROY-CONTENTS-ASYNC: snapshot done, %d victims - staggering deletes", n);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	dc = malloc(sizeof(DestroyCtx));
	dc->victims = snap;
	dc->cursor = GetNextProp(snap);
	dc->container = container;
	dc->onDone = onDone;
	dc->ctx = ctx;
	dc->task = GetTask(ObjGetTaskList());
	AddTaskNow(dc->task, (FuncPtr) DestroyContentsStep, 0, (NodeObj) dc);
}

/*
 * {"cmd":"import-flow"} - drop a saved view onto the canvas as a fresh
 * copy (a clone with a side trip to disk). container/dropX/dropY are
 * where it lands; its own top-level name mints fresh if taken, its
 * internals keep their recorded names verbatim.
 */
NodeObj ImportView(NodeObj container, char *path, char *dropX, char *dropY)
{
	char    containerPath[300], dbg[512];
	FILE   *f;
	long    size;
	char   *text, *cursor, *ap;
	NodeObj deferred, wires;
	NodeObj result = NULL;

	DebugPrint("IMPORT-VIEW enter", __FILE__, __LINE__, IMPORT);

	if (!container || !path || !path[0]
		|| !PathOfInstance(container, containerPath, sizeof(containerPath)))
	{
		DebugPrint("IMPORT-VIEW: bad args or unpathable container, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}

	f = fopen(path, "r");
	if (!f)
	{
		DebugPrint("IMPORT-VIEW: fopen failed, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = malloc(size + 1);
	if (!text)
	{
		fclose(f);
		DebugPrint("IMPORT-VIEW: malloc failed, bail", __FILE__, __LINE__, IMPORT);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW: container='%s' path='%s' size=%ld - parsing", containerPath, path, size);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	cursor = text;
	deferred = NewNode(INTEGER);
	wires = NewNode(INTEGER);

	ap = ImportNode(&cursor, containerPath, deferred, wires, 1, 0);
	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW: ImportNode returned ap=%s", ap ? (ap[0] ? ap : "\"\"") : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (ap && ap[0])
	{
		/* the dropped-in view is born where it was dropped, not at its
		   saved canvas spot - reposition the top node after creation
		   (only the top; a child's X/Y from the file stay relative to
		   its own view, untouched) */
		if (dropX && dropX[0])
		{
			DebugPrint("IMPORT-VIEW: repositioning top to drop point", __FILE__, __LINE__, IMPORT);
			PlaceInstance(ResolvePath(ap), containerPath, dropX, (dropY && dropY[0]) ? dropY : "0");
		}
		DebugPrint("IMPORT-VIEW: running aliases pass", __FILE__, __LINE__, IMPORT);
		ImportAliasesPass(ap, deferred);
		DebugPrint("IMPORT-VIEW: running wires pass", __FILE__, __LINE__, IMPORT);
		ImportWiresPass(ap, wires);
		result = ResolvePath(ap);
	}
	if (ap)
		free(ap);

	DelNode(wires);
	DelNode(deferred);
	free(text);
	snprintf(dbg, sizeof(dbg), "IMPORT-VIEW done: result=%p", (void *) result);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	return result;
}

typedef struct
{
	NodeObj container;
	char    containerPath[300];
	char   *text;
	void  (*onDone)(NodeObj container, int ok, void *ctx);
	void   *ctx;
} LoadViewCtx;

static void LoadView_AfterDestroy(NodeObj container, void *rawCtx)
{
	LoadViewCtx *lv = (LoadViewCtx *) rawCtx;
	char        *cursor, *ap, dbg[512];
	NodeObj      deferred, wires;

	DebugPrint("LOAD-VIEW: destroy done, parsing file into container", __FILE__, __LINE__, IMPORT);

	cursor = lv->text;
	deferred = NewNode(INTEGER);
	wires = NewNode(INTEGER);

	ap = ImportNode(&cursor, lv->containerPath, deferred, wires, 1, 1);	/* skipSelf=1 */
	snprintf(dbg, sizeof(dbg), "LOAD-VIEW: ImportNode(skipSelf) returned ap=%s", ap ? (ap[0] ? ap : "\"\"") : "(null)");
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);
	if (ap)
		free(ap);
	DebugPrint("LOAD-VIEW: running aliases pass", __FILE__, __LINE__, IMPORT);
	ImportAliasesPass(lv->containerPath, deferred);
	DebugPrint("LOAD-VIEW: running wires pass", __FILE__, __LINE__, IMPORT);
	ImportWiresPass(lv->containerPath, wires);

	DelNode(wires);
	DelNode(deferred);
	free(lv->text);
	DebugPrint("LOAD-VIEW done OK", __FILE__, __LINE__, IMPORT);

	{
		void (*onDone)(NodeObj, int, void *) = lv->onDone;
		void  *ctx = lv->ctx;

		free(lv);
		if (onDone)
			onDone(container, 1, ctx);
	}
}

/*
 * {"cmd":"load-flow"} - restore `container` IN PLACE from a whole-session
 * export (ExportView(root, path)): container's current contents are
 * destroyed, then the file's own top-level node (container itself, as
 * ExportView wrote it) is NOT re-created - its children go straight into
 * container, verbatim, since container was just cleared and nothing can
 * collide. This is the "you destroy root and load root in its place"
 * verb - container is not "imported into", it IS what gets restored.
 *
 * Asynchronous (see DestroyContentsAsync's doc comment for why): the
 * destroy runs staggered through the scheduler first, and onDone(container,
 * ok, ctx) fires once the whole restore - destroy AND rebuild - is
 * actually complete. Rebuild itself stays one synchronous recursive-
 * descent pass (LoadView_AfterDestroy) - it's the destroy loop's
 * hundreds of individual DeleteInstance/SndMsg calls that overran the
 * scheduler in one native call, not a single parse of one file.
 */
void LoadViewAsync(NodeObj container, char *path,
					void (*onDone)(NodeObj container, int ok, void *ctx), void *ctx)
{
	char    containerPath[300], dbg[512];
	FILE   *f;
	long    size;
	char   *text;
	LoadViewCtx *lv;

	DebugPrint("LOAD-VIEW-ASYNC enter", __FILE__, __LINE__, IMPORT);

	if (!container || !path || !path[0]
		|| !PathOfInstance(container, containerPath, sizeof(containerPath)))
	{
		DebugPrint("LOAD-VIEW-ASYNC: bad args or unpathable container, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}

	f = fopen(path, "r");
	if (!f)
	{
		DebugPrint("LOAD-VIEW-ASYNC: fopen failed, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = malloc(size + 1);
	if (!text)
	{
		fclose(f);
		DebugPrint("LOAD-VIEW-ASYNC: malloc failed, bail", __FILE__, __LINE__, IMPORT);
		if (onDone)
			onDone(container, 0, ctx);
		return;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	snprintf(dbg, sizeof(dbg), "LOAD-VIEW-ASYNC: container='%s' path='%s' size=%ld - staggering destroy", containerPath, path, size);
	DebugPrint(dbg, __FILE__, __LINE__, IMPORT);

	lv = malloc(sizeof(LoadViewCtx));
	lv->container = container;
	strncpy(lv->containerPath, containerPath, sizeof(lv->containerPath) - 1);
	lv->containerPath[sizeof(lv->containerPath) - 1] = 0;
	lv->text = text;
	lv->onDone = onDone;
	lv->ctx = ctx;

	DestroyContentsAsync(container, LoadView_AfterDestroy, (void *) lv);
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
/* copy ONE node's data - the private building block CloneInstance uses
   for the top instance and for each concrete member it walks. Not a
   public entry: you clone a THING (CloneInstance), which copies the node
   AND whatever the node contains. */
static NodeObj CloneObject(NodeObj source)
{
	NodeObj class, inst, interface, prop, valnode, owner;
	msgobj instanceStart;
	char *name, *val;

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
		if (!name)
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

	NodeObj sub;

	/* ONE wire, not two: the same sink and port recorded twice on one
	   source delivers every message twice. This is reached with the record
	   already present whenever a widget's build re-makes wiring that a load
	   already restored - and a hand cannot draw the same wire twice either.
	   A later call carrying a real handler upgrades the record: that is how
	   a restored wire (Callback 0, the universal default) gets its compiled
	   handler back, since the pointer itself is never saved. */
	for (sub = GetNextProp(fromPort); sub; sub = GetNextSibling(sub))
	{
		char *p;

		if (!CmpName(sub, "Subscriber"))
			continue;
		if ((NodeObj) GetPropLong(sub, "Instance") != toNode)
			continue;
		p = GetPropStr(sub, "Port");
		if ((!p && !toPort) || (p && toPort && strcmp(p, toPort) == 0))
		{
			if (handler)
				SetPropLong(sub, "Callback", handler);
			return;
		}
	}

	sub = NewNode(INTEGER);
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
	char *carry[] = { "Widget", "Label", "X", "Y" };

	if (!propname || !propname[0])
		return NULL;

	linknode = GetPropNode(src, "Value");	/* the alias's doorway slot */
	targetInst = linknode ? (NodeObj) GetPropLong(linknode, "LinkInst") : NULL;
	if (!targetInst)
		return NULL;

	mapped = (NodeObj) GetConnState(map, (long) targetInst);
	if (mapped)
		targetInst = mapped;

	/* Instantiate the Alias the SAME way CloneObject makes a concrete
	   member - directly through the class's InstanceStart, not through
	   CreateObject. During a clone the target container is only a path
	   STRING (nothing here is registered in the path index until the
	   bridge walks the result afterwards), so CreateObject's now-mandatory
	   "resolve the container" check cannot pass and silently dropped every
	   cloned alias. Concrete members never hit that check; the alias must
	   not either. Container is set as a string below, exactly like them. */
	{
		NodeObj aclass = FindClass("Alias");
		msgobj astart = aclass ? (msgobj) GetPropLong(aclass, "InstanceStart") : NULL;
		if (!astart)
			return NULL;
		astart(aclass, msg_initialize, NULL);
		inst = (NodeObj) GetPropLong(aclass, "LastInstance");
	}
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

/* Clone a THING - any instance - into containerPath. There is no separate
   call for a view: you clone the thing, and if the thing contains members
   (a view, or a view inside a view) this copies them too, aliases
   re-pointed and wires re-made, recursing to any depth. A leaf control
   just copies its node and finds no members. One entry, whatever it is. */
NodeObj CloneInstance(NodeObj source, char *containerPath, NodeObj map)
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
		/* ResolvePort says NULL for two different things: the property is
		   absent, or it is present but its link dangles. Only the first is
		   safe to invent - writing 0 over the second destroys both the link
		   and whatever value it was carrying. */
		if (GetPropNode(fromNode, from))
		{
			char dbg[400], fpath[300];
			snprintf(dbg, sizeof(dbg), "Connect: '%s' has property '%s' but it does not "
					 "resolve (dangling link) - refusing to overwrite it",
					 PathOfInstance(fromNode, fpath, sizeof(fpath)) ? fpath
						: (GetPropStr(fromNode, "Name") ? GetPropStr(fromNode, "Name") : "(unnamed)"),
					 from);
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			return 0;
		}
		/* genuinely absent - make the source property exist */
		SetPropInt(fromNode, from, 0);
		fromPort = GetPropNode(fromNode, from);
	}

	/* the target property must already exist */
	toOwner = toNode;
	toPort = ResolvePort(&toOwner, to);
	if (!toPort) {
		char dbg[400], tpath[300];

		snprintf(dbg, sizeof(dbg), "Connect: '%s' has no property '%s'",
				 PathOfInstance(toNode, tpath, sizeof(tpath)) ? tpath
					: (GetPropStr(toNode, "Name") ? GetPropStr(toNode, "Name") : "(unnamed)"),
				 to);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
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

/* allocation accounting - see the twin counter in node.c for the idea.  */
/* An envelope lives only between SndMsg and its DispatchMsg firing, so   */
/* at rest this reads 0; a climb means queued messages are being lost     */
/* (dropped on the ground) instead of delivered-and-freed.                 */
static long envelopesAlive = 0;

long EnvelopeCount(void)
{
	return envelopesAlive;
}

/* scheduler callback: actually walk the subscriber list and deliver.   */
/* Runs from ExecTasks, so every hop is a flat call from the scheduler, */
/* never nested inside the sender's own call stack the way a direct    */
/* SndMsg call used to be                                               */
static int
DispatchMsg(NodeObj envArg, NodeObj unused, int reason){

	MsgEnvelope * env = (MsgEnvelope *) envArg;
	NodeObj sub;

	(void) unused;

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

	envelopesAlive--;
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
	envelopesAlive++;
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
			 GetPropStr(inst, "ReservedViewPanelX"), GetPropStr(inst, "ReservedViewPanelY"));
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
	(void) data;	/* a press is a press, whatever rode in on it */

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

	/* the controls need a home - the target has no path yet in its own
	   InstanceStart, so they go into the settings-home stash view */
	if (!SettingsHome)
		SettingsHome = CreateRoot("Initialize");
	if (!SettingsHome)
		return NULL;

	for (i = 0; i < count; i++)
	{
		control = CreateObject(SettingsHome, specs[i].controlClass);
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
			Connect(control, "Value", target, "Activate");
		else if (IsDisplayControlClass(specs[i].controlClass))
			Connect(target, specs[i].property, control, "Value");
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
	(void) instance;
	(void) propname;
}

/* see the doc comment in object.h - opt-in, ordinary, no different from */
/* any class publishing its own property                                 */
void PublishPosition(NodeObj class)
{
	PublishProp(class, "X", PROP_NULL, "0");
	PublishProp(class, "Y", PROP_NULL, "0");
	PublishProp(class, "W", PROP_NULL, "120");
	PublishProp(class, "H", PROP_NULL, "60");

	/* where something lives is the same kind of fact as where it sits -   */
	/* an ordinary property, not a Slot/membership structure. Empty means   */
	/* "the top-level canvas"; any other value names a View instance's own  */
	/* alias, the same way a wire names an instance - a client renders an   */
	/* instance inside whichever View's Container it currently reads as,    */
	/* correcting late exactly like X/Y already does.                       */
	PublishProp(class, "Container", PROP_NULL, "");

	/* what a thing is CALLED is just one of its properties - writing it   */
	/* renames the instance (the Bridge keys its alias table off it, see   */
	/* Bridge_Set/Bridge_RenameName). Shown as an editable textbox on the  */
	/* dissection table like anything else.                                 */
	PublishProp(class, "Name", PROP_TEXTBOX, "");

	/* every thing is a view: its icon lives wherever Container says, and  */
	/* its open panel is a peer of every other panel at the root, with a   */
	/* position of its own. Open is only the INITIAL presentation - after  */
	/* first paint, open/closed is each window's own business.              */
	/* PROP_ICON is the engine saying what an alias of Open should look     */
	/* like: another icon for the same thing, a doorway to its one panel -   */
	/* the client renders the stamped Widget instead of special-casing the   */
	/* property name.                                                         */
	PublishProp(class, "ReservedViewOpen", PROP_ICON, "0");
	PublishProp(class, "ReservedViewPanelX", PROP_NULL, "240");
	PublishProp(class, "ReservedViewPanelY", PROP_NULL, "60");

	/* generic, not View-specific - anything CAN be marked undeletable      */
	/* this way, the Palette's own bootstrap instances are just the first    */
	/* thing that actually uses it (BuildPalette). Bridge_Delete is what     */
	/* enforces it.                                                          */
	PublishProp(class, "Deletable", PROP_CHECKBOX, "1");
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
	SetPropStr(instance, "ReservedViewOpen", "0");
	SetPropInt(instance, "ReservedViewPanelX", 240);
	SetPropInt(instance, "ReservedViewPanelY", 60);
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
			printf("  %-10s widget=%d default=%s\n",
				GetPropStr(prop, "Name"),
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
	(void) instance;

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
static void
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
	(void) class;
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

NodeObj PublishProp(NodeObj class, char *name, int widget, char *defaultValue){

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
	(void) Instance;
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
