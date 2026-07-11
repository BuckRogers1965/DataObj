#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"
#include "callback.h"

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
		|| strcmp(className, "TCP") == 0;
}

void BuildPalette(void){

	NodeObj library, class, inst;
	int slot;
	char alias[128];

	PaletteView = CreateObject(NULL, "View");
	SetPropStr(PaletteView, "Mode", "Clone");
	SetPropStr(PaletteView, "Deletable", "0");
	SetPropInt(PaletteView, "X", 20);
	SetPropInt(PaletteView, "Y", 20);
	SetPropInt(PaletteView, "W", 190);
	SetPropInt(PaletteView, "H", 220);

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
					SetPropStr(inst, "Deletable", "0");
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
		SetPropStr(modeMenu, "Items", "Operate,Clone,Copy,Move,Connect,Delete,Options");
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

/* record a subscription on a source port */
/* each Subscriber carries the sink instance and the handler */
/* the sink registered as OnMsg on its input port            */
void AddSubscription(NodeObj fromPort, NodeObj toNode, long handler){

	NodeObj sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long)toNode);
	SetPropLong(sub, "Callback", handler);
	AddProp(fromPort, sub);
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	NodeObj fromPort, toPort;
	long handler;

	if (!fromNode || !from || !toNode || !to)
		return 0;

	/* make sure the output port exists on the source */
	fromPort = GetPropNode(fromNode, from);
	if (!fromPort) {
		SetPropInt(fromNode, from, 0);
		fromPort = GetPropNode(fromNode, from);
	}

	/* the target property must already exist */
	toPort = GetPropNode(toNode, to);
	if (!toPort) {
		DebugPrint ( "Connect could not find the input port on the sink.", __FILE__, __LINE__, ERROR);
		return 0;
	}

	handler = GetPropLong(toPort, "OnMsg");
	if (handler)
	{
		AddSubscription(fromPort, toNode, handler);
		return 1;
	}

	/* no compiled handler on this property - every property is both a    */
	/* source and a sink (node.c's own unconditional fan-out already is    */
	/* the source half); ConnectToProperty's generic adapter is the sink    */
	/* half, wired in automatically instead of forcing a caller to know or  */
	/* care whether "to" happens to be a port with real receive logic       */
	/* (Enable) or a plain data property (Filename, a Label's Value) - it   */
	/* is still just "wire these two things together" either way.          */
	return ConnectToProperty(fromNode, from, toNode, to) != NULL;
}

/* route one message out a port to every subscriber of that port */
int
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data){

	NodeObj outPort, sub, toInstance;
	msgobj handler;
	int delivered = 0;

	outPort = GetPropNode(instance, port);
	if (!outPort)
		return 0;

	sub = GetNextProp(outPort);
	while (sub) {
		if (CmpName(sub, "Subscriber")) {
			handler    = (msgobj)  GetPropLong(sub, "Callback");
			toInstance = (NodeObj) GetPropLong(sub, "Instance");
			if (handler) {
				handler(toInstance, message, data);
				delivered++;
			}
		}
		sub = GetNextSibling(sub);
	}

	return delivered;
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
	NodeObj propnode, chunk;

	if (!target || !propname || !value)
		return;

	propnode = GetPropNode(target, propname);
	if (propnode && GetPropLong(propnode, "OnMsg"))
	{
		chunk = NewNode(STRING);
		SetName(chunk, propname);
		SetValueStr(chunk, value);
		DeliverMsg(target, propname, msg_send, chunk);
		DelNode(chunk);
		return;
	}

	SetPropStr(target, propname, value);
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
 * Connect() only works when the target side already has a compiled-in,
 * fixed OnMsg handler - Reader_OnEnable knows, by construction, that it
 * is about "Enable". Wiring a widget's Value into an arbitrary, chosen-
 * at-runtime property (a Textbox's Value into some instance's Filename,
 * say) has no such handler to find: a single generic receiver couldn't
 * know which property name it's for, since AddSubscription only ever
 * hands a handler the owning instance, never which port/property it
 * arrived through. The fix is the same shape as Bridge's subscribe tap:
 * a bare adapter node, not a registered class instance, carrying the
 * target and the property name as its own properties, standing in as
 * the "to" side of an ordinary Connect(). Caller keeps it alive for the
 * life of the wiring, same as a tap.
 */
int PropertyBindingOnMsg(NodeObj adapter, MsgId message, NodeObj data)
{
	NodeObj target;
	char *propName, *value;

	/* no message-type filter, deliberately: a watchable property's own  */
	/* fan-out (PropertyChanged) always arrives as msg_change, not       */
	/* msg_send, and this needs to accept both - the same reasoning      */
	/* Bridge_TapOnIn already settled for subscribe                      */
	target = (NodeObj) GetPropLong(adapter, "Target");
	propName = GetPropStr(adapter, "TargetProp");
	value = GetValueStr(data);

	if (target && propName && value)
		SetOrDeliverProp(target, propName, value);

	return rtrn_handled;
}

NodeObj ConnectToProperty(NodeObj fromInst, char *fromPort, NodeObj targetInst, char *targetProp){

	NodeObj adapter, port;

	if (!fromInst || !fromPort || !targetInst || !targetProp)
		return NULL;

	adapter = NewNode(INTEGER);
	SetName(adapter, "PropertyBinding");
	SetPropLong(adapter, "Target", (long) targetInst);
	SetPropStr(adapter, "TargetProp", targetProp);
	SetPropInt(adapter, "In", 0);
	port = GetPropNode(adapter, "In");
	SetPropLong(port, "OnMsg", (long) PropertyBindingOnMsg);

	if (!Connect(fromInst, fromPort, adapter, "In")) {
		DelNode(adapter);
		return NULL;
	}

	return adapter;
}

/* same idea, for a Button's Out reaching an arbitrary target's Activate - */
/* there is no property to set here, just a function pointer to call      */
int ActivateBindingOnMsg(NodeObj adapter, MsgId message, NodeObj data)
{
	NodeObj target;

	/* no message-type filter, same reasoning as PropertyBindingOnMsg -  */
	/* Button's Out sends msg_send, but a watchable property wired here  */
	/* instead would arrive as msg_change                                */
	target = (NodeObj) GetPropLong(adapter, "Target");
	if (target)
		ActivateInstance(target);

	return rtrn_handled;
}

NodeObj ConnectToActivate(NodeObj fromInst, char *fromPort, NodeObj targetInst){

	NodeObj adapter, port;

	if (!fromInst || !fromPort || !targetInst)
		return NULL;

	adapter = NewNode(INTEGER);
	SetName(adapter, "ActivateBinding");
	SetPropLong(adapter, "Target", (long) targetInst);
	SetPropInt(adapter, "In", 0);
	port = GetPropNode(adapter, "In");
	SetPropLong(port, "OnMsg", (long) ActivateBindingOnMsg);

	if (!Connect(fromInst, fromPort, adapter, "In")) {
		DelNode(adapter);
		return NULL;
	}

	return adapter;
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

		if (strcmp(specs[i].controlClass, "Button") == 0)
			ConnectToActivate(control, "Out", target);
		else if (IsDisplayControlClass(specs[i].controlClass))
			Connect(target, specs[i].property, control, "In");
		else
			ConnectToProperty(control, "Value", target, specs[i].property);
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
	PrintRegInfo("Registering instance of '%s'", Instance);

	AddChild(class, Instance);

	/* leave the newest instance where CreateObject can find it */
	SetPropLong(class, "LastInstance", (long)Instance);

	return Instance;
}

void
UnRegisterInstance(NodeObj class, NodeObj Instance){
    PrintRegInfo("Unregistering instance of '%s'", class);
	//DelNode(node);
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

	DelSibling(instance);
	DelNode(instance);
}